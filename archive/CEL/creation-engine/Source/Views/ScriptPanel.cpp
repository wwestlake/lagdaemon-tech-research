#include "Views/ScriptPanel.h"

#include "engine/script_component.h"
#include "Render/ViewportComponent.h"
#include "Scene/Components.h"

namespace ce {

// Runs ce::engine::IScriptRuntime::Compile on a background thread --
// GS2's Bison/Flex-generated parser is reentrant (no global state), and
// Compile() (Language/src/jit/script_runtime.cpp's CelScriptRuntime) is
// otherwise self-contained (its own LLVMContext/LLJIT per call), so
// nothing here needs any lock beyond the result hand-off itself.
class ScriptPanel::CompileJob final : public juce::Thread {
public:
    CompileJob(ScriptPanel& owner, engine::IScriptRuntime& runtime, entt::entity entity, juce::String source)
        : juce::Thread("CelCompile"), owner_(owner), runtime_(runtime), entity_(entity),
          source_(source.toStdString()) {}

    void run() override {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        std::string error;
        auto compiled = runtime_.Compile(source_, error);
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

        std::lock_guard<std::mutex> lock(owner_.resultMutex_);
        owner_.pendingResult_.entity = entity_;
        owner_.pendingResult_.compiled = compiled;
        owner_.pendingResult_.errorMessage = error;
        owner_.pendingResult_.elapsedMs = elapsedMs;
        owner_.resultReady_.store(true, std::memory_order_release);
    }

private:
    ScriptPanel& owner_;
    engine::IScriptRuntime& runtime_;
    entt::entity entity_;
    std::string source_;
};

ScriptPanel::ScriptPanel(engine::World& world, ViewportComponent& viewport) : world_(world), viewport_(viewport) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    noSelectionLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noSelectionLabel_);

    noScriptLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(noScriptLabel_);
    addAndMakeVisible(attachCombo_);
    attachButton_.onClick = [this] { AttachSelectedCatalogEntry(); };
    addAndMakeVisible(attachButton_);

    editor_.setColourScheme(tokeniser_.getDefaultColourScheme());
    editor_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, 0)));
    addAndMakeVisible(editor_);
    compileButton_.onClick = [this] { CompileCurrent(); };
    addAndMakeVisible(compileButton_);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel_.setText("Not yet compiled", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    UpdateVisibility();
}

ScriptPanel::~ScriptPanel() {
    // A compile job is expected to finish in well under a second (see
    // Language/tests/run_compile_perf_test.cmake's own <1s assertion) --
    // this timeout is just a safety margin for teardown, not a normal
    // code path.
    if (compileJob_ != nullptr) {
        compileJob_->stopThread(2000);
    }
}

void ScriptPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;

    if (selectedEntity_ == entt::null) {
        UpdateVisibility();
        return;
    }

    bool hasSource = false;
    juce::String source;
    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(selectedEntity_)) {
            if (const auto* scriptSource = registry.try_get<scene::ScriptSource>(selectedEntity_)) {
                hasSource = true;
                source = scriptSource->source;
            }
        }
    }

    if (hasSource) {
        document_.replaceAllContent(source);
        document_.clearUndoHistory();
        SetShowingEditor(true);
    } else {
        RebuildAttachCombo();
        SetShowingEditor(false);
    }
}

void ScriptPanel::RebuildAttachCombo() {
    const auto names = viewport_.Scripts().Names();
    if (names == lastShownCatalogNames_) {
        return;
    }
    lastShownCatalogNames_ = names;
    attachCombo_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < names.size(); ++i) {
        attachCombo_.addItem(names[i], static_cast<int>(i) + 1);
    }
    if (!names.empty()) {
        attachCombo_.setSelectedId(1, juce::dontSendNotification);
    }
}

void ScriptPanel::AttachSelectedCatalogEntry() {
    if (selectedEntity_ == entt::null) {
        return;
    }
    const juce::String name = attachCombo_.getText();
    if (name.isEmpty()) {
        return;
    }
    const auto asset = viewport_.Scripts().Find(name);

    {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (!registry.valid(selectedEntity_)) {
            return;
        }
        registry.emplace_or_replace<scene::ScriptSource>(selectedEntity_, scene::ScriptSource{ name, asset.source });
    }

    document_.replaceAllContent(asset.source);
    document_.clearUndoHistory();
    statusLabel_.setText("Attached -- click Compile to activate", juce::dontSendNotification);
    SetShowingEditor(true);
}

void ScriptPanel::CompileCurrent() {
    if (selectedEntity_ == entt::null || compileJob_ != nullptr) {
        return; // one in-flight compile at a time -- compileButton_ is also disabled while running.
    }
    engine::IScriptRuntime* runtime = world_.ScriptRuntime();
    if (runtime == nullptr) {
        statusLabel_.setText("No script runtime available.", juce::dontSendNotification);
        return;
    }

    statusLabel_.setText("Compiling...", juce::dontSendNotification);
    compileButton_.setEnabled(false);
    compileJob_ = std::make_unique<CompileJob>(*this, *runtime, selectedEntity_, document_.getAllContent());
    compileJob_->startThread();
}

void ScriptPanel::Refresh() {
    if (resultReady_.load(std::memory_order_acquire)) {
        PendingResult result;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result = pendingResult_;
            resultReady_.store(false, std::memory_order_release);
        }
        if (compileJob_ != nullptr) {
            compileJob_->stopThread(2000); // already finished (run() returned) -- this just joins.
            compileJob_.reset();
        }
        compileButton_.setEnabled(true);

        if (result.compiled != nullptr) {
            std::lock_guard<std::mutex> lock(world_.RegistryMutex());
            auto& registry = world_.Registry();
            if (registry.valid(result.entity)) {
                // Swap the compiled program WITHOUT resetting `started` --
                // an already-running script keeps running, on_start does
                // NOT refire, and the new on_tick body takes effect the
                // very next Simulation::Step call. This is the "no
                // restart" hot-reload the GS scripting plan calls for.
                auto& sc = registry.get_or_emplace<engine::ScriptComponent>(result.entity);
                sc.compiled = result.compiled;
                sc.faulted = false;
                sc.faultMessage.clear();

                if (auto* source = registry.try_get<scene::ScriptSource>(result.entity)) {
                    // Persist the just-compiled text so re-selecting this
                    // entity later (SetSelectedEntity) shows the version
                    // that's actually running, not a stale one.
                    if (result.entity == selectedEntity_) {
                        source->source = document_.getAllContent();
                    }
                }
            }
            if (result.entity == selectedEntity_) {
                statusLabel_.setText("Compiled in " + juce::String(result.elapsedMs, 1) + " ms",
                                      juce::dontSendNotification);
            }
        } else if (result.entity == selectedEntity_) {
            statusLabel_.setText("Error: " + juce::String(result.errorMessage), juce::dontSendNotification);
        }
    }

    if (selectedEntity_ != entt::null && !showingEditor_) {
        RebuildAttachCombo();
    }
}

void ScriptPanel::SetShowingEditor(bool showingEditor) {
    showingEditor_ = showingEditor;
    UpdateVisibility();
    resized();
}

void ScriptPanel::UpdateVisibility() {
    const bool hasEntity = selectedEntity_ != entt::null;
    noSelectionLabel_.setVisible(!hasEntity);

    const bool attachState = hasEntity && !showingEditor_;
    noScriptLabel_.setVisible(attachState);
    attachCombo_.setVisible(attachState);
    attachButton_.setVisible(attachState);

    const bool editorState = hasEntity && showingEditor_;
    editor_.setVisible(editorState);
    compileButton_.setVisible(editorState);
    statusLabel_.setVisible(editorState);
}

void ScriptPanel::resized() {
    auto bounds = getLocalBounds();
    titleLabel_.setBounds(bounds.removeFromTop(20));

    if (selectedEntity_ == entt::null) {
        noSelectionLabel_.setBounds(bounds.removeFromTop(18));
        return;
    }

    if (!showingEditor_) {
        noScriptLabel_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);
        auto row = bounds.removeFromTop(24);
        attachButton_.setBounds(row.removeFromRight(70));
        row.removeFromRight(6);
        attachCombo_.setBounds(row);
        return;
    }

    statusLabel_.setBounds(bounds.removeFromBottom(18));
    bounds.removeFromBottom(4);
    compileButton_.setBounds(bounds.removeFromBottom(24));
    bounds.removeFromBottom(4);
    editor_.setBounds(bounds);
}

void ScriptPanel::paint(juce::Graphics&) {}

} // namespace ce

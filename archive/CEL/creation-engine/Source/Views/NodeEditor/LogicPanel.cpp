#include "Views/NodeEditor/LogicPanel.h"

#include "engine/script_component.h"
#include "lang/nodegen/graph_to_source.h"
#include "node_system/celg_serialization.h"

namespace ce {

// Mirrors ScriptPanel::CompileJob exactly -- see that class's own
// comment on why no lock beyond the result hand-off is needed
// (Compile() is self-contained per call).
class LogicPanel::CompileJob final : public juce::Thread {
public:
    CompileJob(LogicPanel& owner, engine::IScriptRuntime& runtime, entt::entity entity, std::string source)
        : juce::Thread("CelGraphCompile"), owner_(owner), runtime_(runtime), entity_(entity), source_(std::move(source)) {}

    void run() override {
        std::string error;
        auto compiled = runtime_.Compile(source_, error);

        std::lock_guard<std::mutex> lock(owner_.resultMutex_);
        owner_.pendingResult_.entity = entity_;
        owner_.pendingResult_.compiled = compiled;
        owner_.pendingResult_.errorMessage = error;
        owner_.resultReady_.store(true, std::memory_order_release);
    }

private:
    LogicPanel& owner_;
    engine::IScriptRuntime& runtime_;
    entt::entity entity_;
    std::string source_;
};

LogicPanel::LogicPanel(engine::World& world) : world_(world) {
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    newButton_.onClick = [this] { NewGraph(); };
    addAndMakeVisible(newButton_);
    loadButton_.onClick = [this] { LoadGraph(); };
    addAndMakeVisible(loadButton_);
    saveButton_.onClick = [this] { SaveGraph(); };
    addAndMakeVisible(saveButton_);
    generateButton_.onClick = [this] { GenerateAndShowCode(); };
    addAndMakeVisible(generateButton_);
    compileAttachButton_.onClick = [this] { CompileAndAttach(); };
    addAndMakeVisible(compileAttachButton_);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel_.setText("Ready", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    addAndMakeVisible(palette_);
    addAndMakeVisible(graphComponent_);
    graphComponent_.onSelectionChanged = [this](ce::node_system::NodeId id) { inspector_.SetSelectedNode(id); };
    graphComponent_.onGraphChanged = [this] {
        // A connect/disconnect can change whether the SELECTED node's own
        // pins are connected (e.g. "entity (must be connected)" ->
        // "entity (connected)") -- re-pull the inspector's rows, not just
        // regenerate the code preview, so it never shows stale
        // connected/unconnected state for the node still on screen.
        inspector_.SetSelectedNode(graphComponent_.SelectedNode());
        GenerateAndShowCode();
    };
    addAndMakeVisible(inspector_);
    inspector_.onValueChanged = [this] { GenerateAndShowCode(); };
    addAndMakeVisible(codeView_);
}

LogicPanel::~LogicPanel() {
    if (compileJob_ != nullptr) {
        compileJob_->stopThread(2000);
    }
}

void LogicPanel::SetSelectedEntity(entt::entity entity) {
    selectedEntity_ = entity;
}

void LogicPanel::NewGraph() {
    graph_ = ce::node_system::Graph("untitled");
    currentFilePath_.clear();
    graphComponent_.GraphReplaced();
    inspector_.SetSelectedNode(0);
    codeView_.SetText({});
    statusLabel_.setText("New graph", juce::dontSendNotification);
}

void LogicPanel::LoadGraph() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Load node graph", juce::File(), "*.celg");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this](const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file == juce::File{}) {
                                       return;
                                   }
                                   const auto text = file.loadFileAsString();
                                   std::string error;
                                   auto loaded = ce::node_system::DeserializeGraph(text.toStdString(), error);
                                   if (!loaded) {
                                       statusLabel_.setText("Load failed: " + juce::String(error),
                                                             juce::dontSendNotification);
                                       return;
                                   }
                                   graph_ = std::move(*loaded);
                                   currentFilePath_ = file.getFullPathName();
                                   graphComponent_.GraphReplaced();
                                   inspector_.SetSelectedNode(0);
                                   GenerateAndShowCode();
                                   statusLabel_.setText("Loaded " + file.getFileName(), juce::dontSendNotification);
                               });
}

void LogicPanel::SaveGraph() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Save node graph", juce::File(), "*.celg");
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                               [this](const juce::FileChooser& chooser) {
                                   auto file = chooser.getResult();
                                   if (file == juce::File{}) {
                                       return;
                                   }
                                   if (file.getFileExtension().isEmpty()) {
                                       file = file.withFileExtension("celg");
                                   }
                                   const auto text = ce::node_system::SerializeGraph(graph_);
                                   if (!file.replaceWithText(text)) {
                                       statusLabel_.setText("Save failed", juce::dontSendNotification);
                                       return;
                                   }
                                   currentFilePath_ = file.getFullPathName();
                                   statusLabel_.setText("Saved " + file.getFileName(), juce::dontSendNotification);
                               });
}

void LogicPanel::GenerateAndShowCode() {
    const auto result = ce::lang::nodegen::GenerateSource(graph_, registry_);
    if (!result.ok) {
        juce::String message = "Generation failed:";
        for (const auto& err : result.errors) {
            message += " " + juce::String(err) + ";";
        }
        statusLabel_.setText(message, juce::dontSendNotification);
        codeView_.SetText({});
        graphComponent_.ClearErrorNode();
        return;
    }
    codeView_.SetText(result.source);
    ShowCheckResult(ce::lang::nodegen::CheckGeneratedSource(result));
}

// GS11: runs parse+sema (no JIT) on already-generated source and
// reflects the result in both statusLabel_ and the canvas -- shared by
// GenerateAndShowCode (so the highlight stays live as the user edits,
// not just after a failed Compile & Attach) and CompileAndAttach (which
// also uses this to short-circuit before wasting a JIT compile on
// source it already knows sema will reject).
bool LogicPanel::ShowCheckResult(const ce::lang::nodegen::CheckSourceResult& checkResult) {
    if (checkResult.ok) {
        graphComponent_.ClearErrorNode();
        statusLabel_.setText("Generated OK", juce::dontSendNotification);
        return true;
    }
    const auto& first = checkResult.diagnostics.front();
    graphComponent_.SetErrorNode(first.nodeId);
    const juce::String where = first.nodeId == 0 ? juce::String("(unmapped): ")
                                                  : ("node " + juce::String(first.nodeId) + ": ");
    statusLabel_.setText(where + juce::String(first.message), juce::dontSendNotification);
    return false;
}

void LogicPanel::CompileAndAttach() {
    if (selectedEntity_ == entt::null) {
        statusLabel_.setText("No scene entity selected -- pick one in Scene mode's hierarchy first.",
                              juce::dontSendNotification);
        return;
    }
    if (compileJob_ != nullptr) {
        return; // one in-flight compile at a time, same as ScriptPanel.
    }
    const auto result = ce::lang::nodegen::GenerateSource(graph_, registry_);
    if (!result.ok) {
        GenerateAndShowCode(); // repopulates statusLabel_/codeView_ with the same failure.
        return;
    }
    codeView_.SetText(result.source);
    if (!ShowCheckResult(ce::lang::nodegen::CheckGeneratedSource(result))) {
        return; // sema already rejects this -- no point spending a JIT compile on it.
    }

    engine::IScriptRuntime* runtime = world_.ScriptRuntime();
    if (runtime == nullptr) {
        statusLabel_.setText("No script runtime available.", juce::dontSendNotification);
        return;
    }

    statusLabel_.setText("Compiling...", juce::dontSendNotification);
    compileAttachButton_.setEnabled(false);
    compileJob_ = std::make_unique<CompileJob>(*this, *runtime, selectedEntity_, result.source);
    compileJob_->startThread();
}

void LogicPanel::Refresh() {
    if (!resultReady_.load(std::memory_order_acquire)) {
        return;
    }
    PendingResult result;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result = pendingResult_;
        resultReady_.store(false, std::memory_order_release);
    }
    if (compileJob_ != nullptr) {
        compileJob_->stopThread(2000);
        compileJob_.reset();
    }
    compileAttachButton_.setEnabled(true);

    if (result.compiled != nullptr) {
        std::lock_guard<std::mutex> lock(world_.RegistryMutex());
        auto& registry = world_.Registry();
        if (registry.valid(result.entity)) {
            auto& sc = registry.get_or_emplace<engine::ScriptComponent>(result.entity);
            sc.compiled = result.compiled;
            sc.faulted = false;
            sc.faultMessage.clear();
        }
        statusLabel_.setText("Compiled and attached", juce::dontSendNotification);
    } else {
        statusLabel_.setText("Compile error: " + juce::String(result.errorMessage), juce::dontSendNotification);
    }
}

void LogicPanel::resized() {
    auto bounds = getLocalBounds();
    titleLabel_.setBounds(bounds.removeFromTop(28).removeFromLeft(200));

    auto toolbar = bounds.removeFromTop(28);
    newButton_.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(4);
    loadButton_.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(4);
    saveButton_.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(4);
    generateButton_.setBounds(toolbar.removeFromLeft(80));
    toolbar.removeFromLeft(4);
    compileAttachButton_.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(8);
    statusLabel_.setBounds(toolbar);

    bounds.removeFromTop(6);

    palette_.setBounds(bounds.removeFromLeft(180).reduced(2));

    auto rightColumn = bounds.removeFromRight(320).reduced(2);
    inspector_.setBounds(rightColumn.removeFromTop(rightColumn.getHeight() / 2));
    rightColumn.removeFromTop(4);
    codeView_.setBounds(rightColumn);

    graphComponent_.setBounds(bounds.reduced(2));
}

void LogicPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

} // namespace ce

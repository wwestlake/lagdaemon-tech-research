#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "engine/script_runtime.h"
#include "engine/world.h"
#include "Views/CelCodeTokeniser.h"

namespace ce {

class ViewportComponent;

// GS7: views/edits/recompiles the CEL script attached to whichever
// entity is selected in HierarchyPanel, same selection-driven pattern as
// TransformPanel. Two states:
//   - No scene::ScriptSource on the selected entity: shows an "attach
//     from catalog" combo (populated from viewport_.Scripts(), the
//     ScriptCatalog the Import Hub's ScriptAssetImporter stages `.cel`
//     files into) + Attach button.
//   - Has one: shows a real juce::CodeEditorComponent bound to its
//     source text, a Compile button, and a status line.
//
// Compile() (ce::engine::IScriptRuntime::Compile -- reentrant, see its
// own header comment) runs on a background juce::Thread so a slow
// compile never blocks the message thread/UI. The result is picked up
// by Refresh() (polled at 30Hz from MainComponent, same as every other
// inspector panel) and applied under World::RegistryMutex(): swap
// ce::engine::ScriptComponent::compiled to the freshly-compiled program
// WITHOUT resetting `started` -- so a script already mid-run keeps
// running with its NEW on_tick body starting the very next tick, no
// restart, no re-firing on_start. The OLD compiled program stays alive
// via its own shared_ptr for as long as anything still references it
// (nothing does, once the swap completes) -- ordinary shared_ptr
// lifetime, no extra bookkeeping needed.
class ScriptPanel final : public juce::Component {
public:
    ScriptPanel(engine::World& world, ViewportComponent& viewport);
    ~ScriptPanel() override;

    void SetSelectedEntity(entt::entity entity);

    // Applies any finished background compile, refreshes the status
    // label/attach-vs-edit visibility. Never touches the editor's
    // document content outside of SetSelectedEntity/attach/compile --
    // the user's in-progress typing is never at risk of being stomped by
    // a timer tick, unlike TransformPanel's sliders (which need an
    // explicit "skip while dragging" guard) this panel simply never
    // reloads text on its own.
    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kPreferredHeight = 260;

private:
    class CompileJob;

    void RebuildAttachCombo();
    void AttachSelectedCatalogEntry();
    void CompileCurrent();
    void SetShowingEditor(bool showingEditor);
    void UpdateVisibility();

    engine::World& world_;
    ViewportComponent& viewport_;
    entt::entity selectedEntity_ = entt::null;

    juce::Label titleLabel_{ {}, "Script" };
    juce::Label noSelectionLabel_{ {}, "No entity selected" };

    // "No script attached" state.
    juce::Label noScriptLabel_{ {}, "No script attached" };
    juce::ComboBox attachCombo_;
    juce::TextButton attachButton_{ "Attach" };
    std::vector<juce::String> lastShownCatalogNames_; // avoids rebuilding attachCombo_'s items every 30Hz Refresh() tick.

    // "Has script" state.
    juce::CodeDocument document_;
    CelCodeTokeniser tokeniser_;
    juce::CodeEditorComponent editor_{ document_, &tokeniser_ };
    juce::TextButton compileButton_{ "Compile" };
    juce::Label statusLabel_;

    bool showingEditor_ = false;

    // Background compilation -- see this class's own header comment.
    // resultReady_ is the only thing the background thread touches that
    // the message thread also reads; everything else in pendingResult_
    // is only ever accessed under resultMutex_ (written once by the
    // background thread right before setting resultReady_, read once by
    // Refresh() right after observing it set).
    std::unique_ptr<CompileJob> compileJob_;
    std::atomic<bool> resultReady_{ false };
    std::mutex resultMutex_;
    struct PendingResult {
        entt::entity entity = entt::null;
        std::shared_ptr<engine::ICompiledScript> compiled; // null on failure.
        std::string errorMessage;
        double elapsedMs = 0.0;
    };
    PendingResult pendingResult_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScriptPanel)
};

} // namespace ce

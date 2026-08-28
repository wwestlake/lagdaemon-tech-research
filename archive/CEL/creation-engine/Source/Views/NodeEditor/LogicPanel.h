#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <JuceHeader.h>

#include "engine/script_runtime.h"
#include "engine/world.h"
#include "lang/nodegen/graph_to_source.h"
#include "lang/nodegen/node_catalog.h"
#include "node_system/graph.h"
#include "Views/NodeEditor/GeneratedCodeView.h"
#include "Views/NodeEditor/NodeGraphComponent.h"
#include "Views/NodeEditor/NodeInspector.h"
#include "Views/NodeEditor/NodePalette.h"

namespace ce {

// GS10: the Logic workspace mode's top-level panel -- owns the live
// ce::node_system::Graph document, the v1 node catalog, and every
// sub-panel (palette/canvas/inspector/generated-code view), plus the
// toolbar (New/Load/Save/Generate/Compile & Attach). PUBLICLY inherits
// juce::DragAndDropContainer (must be public, not private -- JUCE's
// DragAndDropContainer::findParentDragContainerFor walks the component
// tree using dynamic_cast<DragAndDropContainer*>, and dynamic_cast to a
// pointer type only succeeds through a PUBLIC base per the C++
// standard; a private base here would make NodePalette's drag silently
// never start) so NodePalette's drag lands in NodeGraphComponent (see
// NodePalette's own comment; this is this codebase's first generic
// Component-to-Component DnD use, HierarchyPanel's is TreeView-internal
// only).
//
// "Compile & Attach" mirrors ScriptPanel's exact background-compile
// pattern (GenerateSource is cheap/local and runs synchronously on the
// message thread; only IScriptRuntime::Compile -- the LLVM JIT part --
// runs on a background juce::Thread, picked up by Refresh() same as
// ScriptPanel/TransformPanel's 30Hz poll from MainComponent).
class LogicPanel final : public juce::Component, public juce::DragAndDropContainer {
public:
    explicit LogicPanel(engine::World& world);
    ~LogicPanel() override;

    // The SCENE entity Compile & Attach targets -- same explicit-push
    // selection pattern as TransformPanel/ScriptPanel, wired from
    // MainComponent's hierarchyPanel_.onSelectionChanged. Unrelated to
    // NodeGraphComponent's own "selected node within the graph" concept.
    void SetSelectedEntity(entt::entity entity);

    void Refresh();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class CompileJob;

    void NewGraph();
    void LoadGraph();
    void SaveGraph();
    void GenerateAndShowCode();
    // GS11: applies a CheckGeneratedSource result to statusLabel_ and
    // the canvas's error-node highlight. Returns true iff clean (no
    // diagnostics) -- callers use this to decide whether it's worth
    // proceeding to an actual (expensive) JIT compile.
    bool ShowCheckResult(const ce::lang::nodegen::CheckSourceResult& checkResult);
    void CompileAndAttach();

    engine::World& world_;
    entt::entity selectedEntity_ = entt::null;

    ce::node_system::NodeTypeRegistry registry_ = ce::lang::nodegen::BuildCoreNodeCatalog();
    ce::node_system::Graph graph_{ "untitled" };
    juce::String currentFilePath_;

    juce::Label titleLabel_{ {}, "Logic" };
    juce::TextButton newButton_{ "New" };
    juce::TextButton loadButton_{ "Load..." };
    juce::TextButton saveButton_{ "Save..." };
    juce::TextButton generateButton_{ "Generate" };
    juce::TextButton compileAttachButton_{ "Compile & Attach" };
    juce::Label statusLabel_;

    NodePalette palette_{ registry_ };
    NodeGraphComponent graphComponent_{ graph_, registry_ };
    NodeInspector inspector_{ graph_ };
    GeneratedCodeView codeView_;

    std::unique_ptr<juce::FileChooser> fileChooser_; // must outlive the async load/save callback.

    std::unique_ptr<CompileJob> compileJob_;
    std::atomic<bool> resultReady_{ false };
    std::mutex resultMutex_;
    struct PendingResult {
        entt::entity entity = entt::null;
        std::shared_ptr<engine::ICompiledScript> compiled;
        std::string errorMessage;
    };
    PendingResult pendingResult_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LogicPanel)
};

} // namespace ce

#pragma once

#include <memory>
#include <vector>

#include <JuceHeader.h>

#include "node_system/graph.h"

namespace ce {

// GS10: edits the selected node's UNCONNECTED Data input pins --
// exactly the per-instance "configuration" convention GS9's node
// catalog relies on (a Log node's literal message, a GetVariable's
// variable name, an ordinary Compare node's threshold -- see
// Language/include/lang/nodegen/node_catalog.h's own comment). A
// connected input shows "(connected)" instead of an editable field,
// since its default is never consulted by codegen once wired. Exec
// pins and Entity-typed pins (no CEL literal exists -- see
// docs/SCRIPTING_ABI.md) are never editable.
//
// Unlike TransformPanel (writes into World's entt registry, contended
// with the render thread, hence RegistryMutex()), ce::node_system::Graph
// is message-thread-only -- no lock needed to mutate a Pin's
// defaultValue directly (see GS10 investigation notes).
class NodeInspector final : public juce::Component {
public:
    explicit NodeInspector(ce::node_system::Graph& graph);

    void SetSelectedNode(ce::node_system::NodeId nodeId);

    // Fired whenever a field commits a new value into a Pin's
    // defaultValue -- LogicPanel uses this to regenerate the code
    // preview/error highlight, the same way NodeGraphComponent's
    // onGraphChanged does for wiring/add/remove. Editing a pin default
    // changes generated source just as much as a wire does, and without
    // this callback nothing tells LogicPanel a re-generate is needed.
    std::function<void()> onValueChanged;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kRowHeight = 22;

private:
    void RebuildRows();
    void AddUneditableRow(const juce::String& label);
    void AddFloatRow(const juce::String& label, ce::node_system::PinId pinId, bool isVec3Component, int vec3Index);
    void AddIntRow(const juce::String& label, ce::node_system::PinId pinId);
    void AddBoolRow(const juce::String& label, ce::node_system::PinId pinId);
    void AddStringRow(const juce::String& label, ce::node_system::PinId pinId);

    ce::node_system::Graph& graph_;
    ce::node_system::NodeId selectedNode_ = 0;

    juce::Label titleLabel_{ {}, "Inspector" };
    juce::Label noSelectionLabel_{ {}, "No node selected" };
    juce::Label nodeTypeLabel_;

    struct Row {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> editor; // null for an uneditable informational row.
    };
    std::vector<Row> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeInspector)
};

} // namespace ce

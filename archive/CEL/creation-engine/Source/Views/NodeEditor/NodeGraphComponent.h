#pragma once

#include <JuceHeader.h>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

namespace ce {

// GS10: the interactive 2D canvas -- pan/zoom, node selection/drag,
// wire drag-to-connect, delete, and accepting a dropped node type from
// NodePalette. Owns no state of its own beyond view (pan/zoom) and
// interaction (drag/selection) -- the actual document is `graph_`, a
// reference to the ce::node_system::Graph LogicPanel owns, so Save/Load
// (LogicPanel swapping the whole Graph out) just works without this
// component needing to know about files at all.
//
// Unlike ViewportComponent (pure OpenGL, camera control lives in a
// separate FreeCamera/MouseListener because its state is shared with
// the render thread), this component is ordinary message-thread-only
// juce::Component painting with Graphics -- no GL, no other thread ever
// touches `graph_`, so plain direct mouseDown/mouseDrag/paint overrides
// are enough; there's no ViewportComponent-shaped precedent to mirror
// here (see GS10 investigation notes).
class NodeGraphComponent final : public juce::Component, public juce::DragAndDropTarget {
public:
    NodeGraphComponent(ce::node_system::Graph& graph, const ce::node_system::NodeTypeRegistry& registry);

    // Fired on every selection change, including to "none" (id 0 -- a
    // real NodeId is never 0, see Graph::AddNode's auto-increment
    // starting at 1). Same explicit-push pattern as HierarchyPanel's
    // onSelectionChanged -- there is no ambient shared-selection object
    // in this codebase (see GS10 investigation notes).
    std::function<void(ce::node_system::NodeId)> onSelectionChanged;

    // Fired after any edit that changes the graph's shape or wiring
    // (node added/removed/moved is NOT included -- only add/remove/
    // connect/disconnect, the things that actually change generated
    // source) -- LogicPanel uses this to invalidate its cached
    // generated-code preview rather than silently going stale.
    std::function<void()> onGraphChanged;

    ce::node_system::NodeId SelectedNode() const { return selectedNode_; }
    void ClearSelection();

    // GS11: highlights the node CheckGeneratedSource attributed the
    // current diagnostic to (a red outline, drawn in addition to --
    // not instead of -- the normal selection outline), or clears it.
    // LogicPanel re-derives this after every graph edit, so it always
    // reflects the LATEST check, not a stale one.
    void SetErrorNode(ce::node_system::NodeId id) { errorNode_ = id; repaint(); }
    void ClearErrorNode() { SetErrorNode(0); }

    // Re-reads `graph_` from scratch (e.g. after LogicPanel replaces it
    // wholesale via Load) -- resets selection, keeps the current
    // pan/zoom so loading a graph doesn't disorient the view.
    void GraphReplaced();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // juce::DragAndDropTarget -- accepts a plain string node-type-name
    // description dragged from NodePalette (see that class's own
    // comment: this is this codebase's first use of generic
    // Component-to-Component DnD, HierarchyPanel's is TreeView-only).
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    struct PinHit {
        ce::node_system::NodeId nodeId = 0;
        ce::node_system::PinId pinId = 0;
        bool isInput = false;
    };

    juce::Point<float> WorldToScreen(juce::Point<float> world) const;
    juce::Point<float> ScreenToWorld(juce::Point<float> screen) const;
    juce::Rectangle<float> NodeScreenBounds(const ce::node_system::Node& node) const;
    float NodeWorldHeight(const ce::node_system::Node& node) const;
    juce::Point<float> PinScreenPos(const ce::node_system::Node& node, const ce::node_system::Pin& pin,
                                     std::size_t rowIndex) const;

    ce::node_system::NodeId HitTestNode(juce::Point<float> screenPos) const;
    bool HitTestPin(juce::Point<float> screenPos, PinHit& outHit) const;

    void SelectNode(ce::node_system::NodeId id);
    void DrawNode(juce::Graphics& g, const ce::node_system::Node& node);
    void DrawWire(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour);

    ce::node_system::Graph& graph_;
    const ce::node_system::NodeTypeRegistry& registry_;

    juce::Point<float> viewOffset_{ 40.0f, 40.0f };
    float zoom_ = 1.0f;

    ce::node_system::NodeId selectedNode_ = 0;
    ce::node_system::NodeId errorNode_ = 0;

    bool panning_ = false;
    juce::Point<float> panStartMouse_;
    juce::Point<float> panStartOffset_;

    bool draggingNode_ = false;
    ce::node_system::NodeId dragNodeId_ = 0;
    juce::Point<float> dragStartMouseWorld_;
    juce::Point<float> dragStartNodeWorld_;

    bool draggingWire_ = false;
    PinHit wireStart_;
    juce::Point<float> wireDragScreenPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeGraphComponent)
};

} // namespace ce

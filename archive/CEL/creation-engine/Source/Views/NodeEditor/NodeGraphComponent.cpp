#include "Views/NodeEditor/NodeGraphComponent.h"

#include <algorithm>
#include <cmath>

namespace ce {

namespace {

using ce::node_system::Connection;
using ce::node_system::Node;
using ce::node_system::NodeId;
using ce::node_system::Pin;
using ce::node_system::PinId;
using ce::node_system::PinKind;

constexpr float kNodeWidth = 170.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr float kRowHeight = 20.0f;
constexpr float kBottomPadding = 8.0f;
constexpr float kPinRadius = 5.0f;
constexpr float kPinHitPadding = 5.0f;
constexpr float kMinZoom = 0.35f;
constexpr float kMaxZoom = 2.5f;

juce::Colour HeaderColourFor(ce::node_system::Domain domain) {
    switch (domain) {
        case ce::node_system::Domain::Event: return juce::Colour(0xffb8722f);
        case ce::node_system::Domain::Core: return juce::Colour(0xff2f6fb8);
        case ce::node_system::Domain::Animation: return juce::Colour(0xff5f9c5a);
        case ce::node_system::Domain::Material: return juce::Colour(0xff9c5a9c);
        case ce::node_system::Domain::Audio: return juce::Colour(0xff9c8a5a);
    }
    return juce::Colour(0xff444444);
}

juce::Colour PinColourFor(const Pin& pin) {
    return pin.type.kind == PinKind::Exec ? juce::Colours::white : juce::Colour(0xff7fd0e8);
}

} // namespace

NodeGraphComponent::NodeGraphComponent(ce::node_system::Graph& graph, const ce::node_system::NodeTypeRegistry& registry)
    : graph_(graph), registry_(registry) {
    setWantsKeyboardFocus(true);
}

void NodeGraphComponent::ClearSelection() {
    SelectNode(0);
}

void NodeGraphComponent::GraphReplaced() {
    selectedNode_ = 0;
    errorNode_ = 0;
    draggingNode_ = false;
    draggingWire_ = false;
    panning_ = false;
    if (onSelectionChanged) {
        onSelectionChanged(0);
    }
    repaint();
}

juce::Point<float> NodeGraphComponent::WorldToScreen(juce::Point<float> world) const {
    return { world.x * zoom_ + viewOffset_.x, world.y * zoom_ + viewOffset_.y };
}

juce::Point<float> NodeGraphComponent::ScreenToWorld(juce::Point<float> screen) const {
    return { (screen.x - viewOffset_.x) / zoom_, (screen.y - viewOffset_.y) / zoom_ };
}

float NodeGraphComponent::NodeWorldHeight(const Node& node) const {
    const std::size_t rows = std::max(node.Inputs().size(), node.Outputs().size());
    return kHeaderHeight + static_cast<float>(rows) * kRowHeight + kBottomPadding;
}

juce::Rectangle<float> NodeGraphComponent::NodeScreenBounds(const Node& node) const {
    const auto topLeft = WorldToScreen({ node.EditorX(), node.EditorY() });
    return { topLeft.x, topLeft.y, kNodeWidth * zoom_, NodeWorldHeight(node) * zoom_ };
}

juce::Point<float> NodeGraphComponent::PinScreenPos(const Node& node, const Pin& pin, std::size_t rowIndex) const {
    const float localX = pin.isInput ? 0.0f : kNodeWidth;
    const float localY = kHeaderHeight + (static_cast<float>(rowIndex) + 0.5f) * kRowHeight;
    return WorldToScreen({ node.EditorX() + localX, node.EditorY() + localY });
}

NodeId NodeGraphComponent::HitTestNode(juce::Point<float> screenPos) const {
    for (const auto& [id, node] : graph_.Nodes()) {
        if (NodeScreenBounds(*node).contains(screenPos)) {
            return id;
        }
    }
    return 0;
}

bool NodeGraphComponent::HitTestPin(juce::Point<float> screenPos, PinHit& outHit) const {
    const float radius = kPinRadius * zoom_ + kPinHitPadding;
    for (const auto& [id, node] : graph_.Nodes()) {
        const auto& inputs = node->Inputs();
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            if (PinScreenPos(*node, inputs[i], i).getDistanceFrom(screenPos) <= radius) {
                outHit = { id, inputs[i].id, true };
                return true;
            }
        }
        const auto& outputs = node->Outputs();
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (PinScreenPos(*node, outputs[i], i).getDistanceFrom(screenPos) <= radius) {
                outHit = { id, outputs[i].id, false };
                return true;
            }
        }
    }
    return false;
}

void NodeGraphComponent::SelectNode(NodeId id) {
    if (selectedNode_ == id) {
        return;
    }
    selectedNode_ = id;
    if (onSelectionChanged) {
        onSelectionChanged(id);
    }
    repaint();
}

void NodeGraphComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff11151b));

    // Faint dot grid, purely decorative -- helps make pan/zoom legible.
    g.setColour(juce::Colour(0xff1c232d));
    const float gridSpacing = 32.0f * zoom_;
    if (gridSpacing > 6.0f) {
        for (float x = std::fmod(viewOffset_.x, gridSpacing); x < static_cast<float>(getWidth()); x += gridSpacing) {
            for (float y = std::fmod(viewOffset_.y, gridSpacing); y < static_cast<float>(getHeight()); y += gridSpacing) {
                g.fillRect(x, y, 2.0f, 2.0f);
            }
        }
    }

    for (const Connection& conn : graph_.Connections()) {
        const Node* fromNode = graph_.FindNode(conn.fromNode);
        const Node* toNode = graph_.FindNode(conn.toNode);
        if (fromNode == nullptr || toNode == nullptr) {
            continue;
        }
        const Pin* fromPin = fromNode->FindPin(conn.fromPin);
        const Pin* toPin = toNode->FindPin(conn.toPin);
        if (fromPin == nullptr || toPin == nullptr) {
            continue;
        }
        const auto& fromOutputs = fromNode->Outputs();
        const auto& toInputs = toNode->Inputs();
        const auto fromIt = std::find_if(fromOutputs.begin(), fromOutputs.end(),
                                          [&](const Pin& p) { return p.id == fromPin->id; });
        const auto toIt =
            std::find_if(toInputs.begin(), toInputs.end(), [&](const Pin& p) { return p.id == toPin->id; });
        if (fromIt == fromOutputs.end() || toIt == toInputs.end()) {
            continue;
        }
        const auto fromPos = PinScreenPos(*fromNode, *fromPin, static_cast<std::size_t>(fromIt - fromOutputs.begin()));
        const auto toPos = PinScreenPos(*toNode, *toPin, static_cast<std::size_t>(toIt - toInputs.begin()));
        DrawWire(g, fromPos, toPos, PinColourFor(*fromPin));
    }

    if (draggingWire_) {
        const Node* startNode = graph_.FindNode(wireStart_.nodeId);
        if (startNode != nullptr) {
            const Pin* startPin = startNode->FindPin(wireStart_.pinId);
            if (startPin != nullptr) {
                const auto& pins = wireStart_.isInput ? startNode->Inputs() : startNode->Outputs();
                const auto it = std::find_if(pins.begin(), pins.end(), [&](const Pin& p) { return p.id == startPin->id; });
                if (it != pins.end()) {
                    const auto startPos = PinScreenPos(*startNode, *startPin, static_cast<std::size_t>(it - pins.begin()));
                    DrawWire(g, wireStart_.isInput ? wireDragScreenPos_ : startPos,
                              wireStart_.isInput ? startPos : wireDragScreenPos_, juce::Colours::white);
                }
            }
        }
    }

    std::vector<NodeId> nodeIds;
    nodeIds.reserve(graph_.Nodes().size());
    for (const auto& [id, node] : graph_.Nodes()) {
        nodeIds.push_back(id);
    }
    std::sort(nodeIds.begin(), nodeIds.end());
    for (NodeId id : nodeIds) {
        DrawNode(g, *graph_.FindNode(id));
    }
}

void NodeGraphComponent::DrawWire(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour) {
    juce::Path path;
    path.startNewSubPath(from);
    const float dx = std::max(30.0f, std::abs(to.x - from.x) * 0.5f);
    path.cubicTo(from.x + dx, from.y, to.x - dx, to.y, to.x, to.y);
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void NodeGraphComponent::DrawNode(juce::Graphics& g, const Node& node) {
    const auto bounds = NodeScreenBounds(node);
    const bool selected = node.Id() == selectedNode_;

    g.setColour(juce::Colour(0xff1d2530));
    g.fillRoundedRectangle(bounds, 6.0f);

    auto headerBounds = bounds.withHeight(kHeaderHeight * zoom_);
    g.setColour(HeaderColourFor(node.NodeDomain()));
    g.fillRoundedRectangle(headerBounds, 6.0f);
    g.fillRect(headerBounds.withTop(headerBounds.getBottom() - 6.0f * zoom_)); // square off the rounded bottom corners.

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(std::max(10.0f, 13.0f * zoom_))).boldened());
    g.drawText(node.TypeName(), headerBounds.reduced(6.0f * zoom_, 0.0f), juce::Justification::centredLeft, true);

    g.setColour(selected ? juce::Colours::white : juce::Colour(0xff384354));
    g.drawRoundedRectangle(bounds, 6.0f, selected ? 2.0f : 1.0f);

    // GS11: drawn ADDITIONALLY (a slightly larger, outer red ring), not
    // instead of the selection outline -- a node can be both selected
    // AND the current diagnostic's target at once, and both facts stay
    // visible.
    if (node.Id() == errorNode_) {
        g.setColour(juce::Colours::red);
        g.drawRoundedRectangle(bounds.expanded(2.5f), 7.0f, 2.0f);
    }

    g.setFont(juce::Font(juce::FontOptions(std::max(9.0f, 11.0f * zoom_))));
    const auto& inputs = node.Inputs();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto pos = PinScreenPos(node, inputs[i], i);
        g.setColour(PinColourFor(inputs[i]));
        g.fillEllipse(pos.x - kPinRadius * zoom_, pos.y - kPinRadius * zoom_, kPinRadius * 2.0f * zoom_,
                       kPinRadius * 2.0f * zoom_);
        g.setColour(juce::Colours::lightgrey);
        g.drawText(inputs[i].name,
                   juce::Rectangle<float>(pos.x + 8.0f * zoom_, pos.y - kRowHeight * zoom_ * 0.5f,
                                           (kNodeWidth * 0.5f) * zoom_, kRowHeight * zoom_),
                   juce::Justification::centredLeft, true);
    }
    const auto& outputs = node.Outputs();
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto pos = PinScreenPos(node, outputs[i], i);
        g.setColour(PinColourFor(outputs[i]));
        g.fillEllipse(pos.x - kPinRadius * zoom_, pos.y - kPinRadius * zoom_, kPinRadius * 2.0f * zoom_,
                       kPinRadius * 2.0f * zoom_);
        g.setColour(juce::Colours::lightgrey);
        g.drawText(outputs[i].name,
                   juce::Rectangle<float>(pos.x - (kNodeWidth * 0.5f + 8.0f) * zoom_, pos.y - kRowHeight * zoom_ * 0.5f,
                                           (kNodeWidth * 0.5f) * zoom_, kRowHeight * zoom_),
                   juce::Justification::centredRight, true);
    }
}

void NodeGraphComponent::resized() {}

void NodeGraphComponent::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    const auto screenPos = event.position;

    PinHit hit;
    if (HitTestPin(screenPos, hit)) {
        draggingWire_ = true;
        wireStart_ = hit;
        wireDragScreenPos_ = screenPos;
        repaint();
        return;
    }

    const NodeId hitNode = HitTestNode(screenPos);
    if (hitNode != 0) {
        SelectNode(hitNode);
        draggingNode_ = true;
        dragNodeId_ = hitNode;
        dragStartMouseWorld_ = ScreenToWorld(screenPos);
        const Node* node = graph_.FindNode(hitNode);
        dragStartNodeWorld_ = { node->EditorX(), node->EditorY() };
        return;
    }

    SelectNode(0);
    panning_ = true;
    panStartMouse_ = screenPos;
    panStartOffset_ = viewOffset_;
}

void NodeGraphComponent::mouseDrag(const juce::MouseEvent& event) {
    if (draggingWire_) {
        wireDragScreenPos_ = event.position;
        repaint();
        return;
    }
    if (draggingNode_) {
        Node* node = graph_.FindNode(dragNodeId_);
        if (node != nullptr) {
            const auto worldNow = ScreenToWorld(event.position);
            const auto delta = worldNow - dragStartMouseWorld_;
            node->SetEditorPosition(dragStartNodeWorld_.x + delta.x, dragStartNodeWorld_.y + delta.y);
            repaint();
        }
        return;
    }
    if (panning_) {
        viewOffset_ = panStartOffset_ + (event.position - panStartMouse_);
        repaint();
    }
}

void NodeGraphComponent::mouseUp(const juce::MouseEvent& event) {
    if (draggingWire_) {
        draggingWire_ = false;
        PinHit target;
        if (HitTestPin(event.position, target) && target.nodeId != 0) {
            // Normalize to (output -> input) regardless of which end the
            // drag started from -- a user can just as naturally drag from
            // an input backwards to an output.
            PinHit outputEnd = wireStart_.isInput ? target : wireStart_;
            PinHit inputEnd = wireStart_.isInput ? wireStart_ : target;
            if (!outputEnd.isInput && inputEnd.isInput && !(outputEnd.nodeId == inputEnd.nodeId && outputEnd.pinId == inputEnd.pinId)) {
                // An input accepts at most one incoming wire (the codegen
                // walk assumes this -- see graph_to_source.cpp's own
                // ambiguous-connection error) -- dragging a new wire onto
                // an already-connected input replaces the old one, the
                // expected UX for "rewiring."
                std::vector<ce::node_system::ConnectionId> toRemove;
                for (const Connection& conn : graph_.Connections()) {
                    if (conn.toNode == inputEnd.nodeId && conn.toPin == inputEnd.pinId) {
                        toRemove.push_back(conn.id);
                    }
                }
                for (auto id : toRemove) {
                    graph_.Disconnect(id);
                }
                ce::node_system::ConnectError error{};
                const auto result = graph_.Connect(outputEnd.nodeId, outputEnd.pinId, inputEnd.nodeId, inputEnd.pinId, &error);
                if (result && onGraphChanged) {
                    onGraphChanged();
                }
            }
        }
        repaint();
        return;
    }
    draggingNode_ = false;
    panning_ = false;
}

void NodeGraphComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    const auto worldBefore = ScreenToWorld(event.position);
    const float factor = wheel.deltaY > 0.0f ? 1.1f : (1.0f / 1.1f);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    const auto screenAfter = WorldToScreen(worldBefore);
    viewOffset_ += event.position - screenAfter;
    repaint();
}

bool NodeGraphComponent::keyPressed(const juce::KeyPress& key) {
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedNode_ != 0) {
        graph_.RemoveNode(selectedNode_);
        selectedNode_ = 0;
        if (onSelectionChanged) {
            onSelectionChanged(0);
        }
        if (onGraphChanged) {
            onGraphChanged();
        }
        repaint();
        return true;
    }
    return false;
}

bool NodeGraphComponent::isInterestedInDragSource(const SourceDetails& details) {
    return details.description.isString();
}

void NodeGraphComponent::itemDropped(const SourceDetails& details) {
    const auto typeName = details.description.toString().toStdString();
    std::string error;
    Node* node = ce::node_system::AddRegisteredNode(graph_, registry_, typeName, &error);
    if (node == nullptr) {
        return;
    }
    const auto world = ScreenToWorld(details.localPosition.toFloat());
    node->SetEditorPosition(world.x, world.y);
    SelectNode(node->Id());
    if (onGraphChanged) {
        onGraphChanged();
    }
    repaint();
}

} // namespace ce

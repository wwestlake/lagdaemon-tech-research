#pragma once

#include <vector>

#include <JuceHeader.h>

#include "node_system/type_registry.h"

namespace ce {

// GS10: a scrollable, draggable list of every type registered in the
// catalog (ce::lang::nodegen::BuildCoreNodeCatalog()) -- drag a row
// onto NodeGraphComponent to create a node of that type. Relies on
// juce::ListBox's built-in drag support (ListBoxModel::
// getDragSourceDescription returning non-void triggers it automatically
// via the nearest ancestor juce::DragAndDropContainer -- LogicPanel is
// that ancestor, see its own comment) rather than hand-rolled
// mouseDrag/startDragging.
class NodePalette final : public juce::Component, private juce::ListBoxModel {
public:
    explicit NodePalette(const ce::node_system::NodeTypeRegistry& registry);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;

    std::vector<std::string> typeNames_;
    juce::Label titleLabel_{ {}, "Nodes" };
    juce::ListBox listBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodePalette)
};

} // namespace ce

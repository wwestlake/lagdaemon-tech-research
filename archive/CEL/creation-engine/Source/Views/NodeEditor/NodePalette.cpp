#include "Views/NodeEditor/NodePalette.h"

#include <algorithm>

namespace ce {

NodePalette::NodePalette(const ce::node_system::NodeTypeRegistry& registry) : listBox_({}, this) {
    for (const auto& [typeName, descriptor] : registry.Types()) {
        typeNames_.push_back(typeName);
    }
    std::sort(typeNames_.begin(), typeNames_.end());

    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    listBox_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff181c22));
    listBox_.setRowHeight(24);
    addAndMakeVisible(listBox_);
}

int NodePalette::getNumRows() {
    return static_cast<int>(typeNames_.size());
}

void NodePalette::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(typeNames_.size())) {
        return;
    }
    if (rowIsSelected) {
        g.fillAll(juce::Colour(0xff2a3644));
    }
    g.setColour(juce::Colour(0xffb8c4d5));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(typeNames_[static_cast<std::size_t>(rowNumber)], 8, 0, width - 8, height,
               juce::Justification::centredLeft, true);
}

juce::var NodePalette::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
    if (selectedRows.isEmpty()) {
        return {};
    }
    const int row = selectedRows[0];
    if (row < 0 || row >= static_cast<int>(typeNames_.size())) {
        return {};
    }
    return juce::String(typeNames_[static_cast<std::size_t>(row)]);
}

void NodePalette::resized() {
    auto bounds = getLocalBounds();
    titleLabel_.setBounds(bounds.removeFromTop(24));
    listBox_.setBounds(bounds);
}

void NodePalette::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

} // namespace ce

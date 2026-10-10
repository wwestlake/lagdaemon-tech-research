#pragma once

// The external pin layout editor, one for Sub Diagram blocks and FRust
// programmable components (schematic::BlockPort, the common pin layout):
// the pins in a list, a side for the selected pin, Earlier / Later along its
// side, a preview of the symbol drawn as the canvas draws it, and Save. Only
// sides and orders change; which terminal each pin is never does.

#include <JuceHeader.h>

#include "SchematicSymbols.h"

#include <functional>
#include <memory>
#include <vector>

class PinLayoutEditor final : public juce::Component
{
public:
    using SaveFn = std::function<bool(const std::vector<schematic::BlockPort>& layout, juce::String& error)>;

    PinLayoutEditor(juce::String name, juce::String symbolId, std::vector<schematic::BlockPort> ports, SaveFn onSave);
    ~PinLayoutEditor() override;

    // Opens the editor in its own window.
    static void show(juce::String name, juce::String symbolId, std::vector<schematic::BlockPort> ports, SaveFn onSave);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;
    void select(int pin);
    void move(int delta);
    void refresh();

    juce::String name, symbolId;
    std::vector<schematic::BlockPort> ports;
    SaveFn onSave;
    int selected = 0;

    std::unique_ptr<Rows> rows;
    juce::ListBox list;
    juce::ComboBox side;
    juce::TextButton earlier { "Earlier" }, later { "Later" }, save { "Save" }, cancel { "Cancel" };
    juce::Label hint;
    juce::Rectangle<int> previewArea;
};

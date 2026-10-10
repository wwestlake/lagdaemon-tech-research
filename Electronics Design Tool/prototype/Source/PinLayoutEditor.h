#pragma once

// The external pin layout editor, one for Sub Diagram blocks and FRust
// programmable components (schematic::BlockPort, the common pin layout):
// the pins in a list, a side for the selected pin, Earlier / Later along its
// side, a preview of the symbol drawn as the canvas draws it, and Save.
// Moving a pin never changes which terminal it is. For Sub Diagram blocks
// (pinsEditable) pins can also be added, renamed and removed: each pin keeps
// its stable id (BlockPort::id; a new pin has none until saved), so a rename
// keeps every wire, and a removed pin takes only its own wires with it.

#include <JuceHeader.h>

#include "SchematicSymbols.h"

#include <functional>
#include <memory>
#include <vector>

class PinLayoutEditor final : public juce::Component
{
public:
    using SaveFn = std::function<bool(const std::vector<schematic::BlockPort>& layout, juce::String& error)>;

    PinLayoutEditor(juce::String name, juce::String symbolId, std::vector<schematic::BlockPort> ports, SaveFn onSave, bool pinsEditable = false);
    ~PinLayoutEditor() override;

    // Opens the editor in its own window.
    static void show(juce::String name, juce::String symbolId, std::vector<schematic::BlockPort> ports, SaveFn onSave, bool pinsEditable = false);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;
    void select(int pin);
    void move(int delta);
    void refresh();
    void addPin();
    void renamePin();
    void removePin();
    bool checkName(const juce::String& text, int except);
    void showError(const juce::String& text);

    juce::String name, symbolId;
    std::vector<schematic::BlockPort> ports;
    SaveFn onSave;
    int selected = 0;
    bool pinsEditable = false;

    std::unique_ptr<Rows> rows;
    juce::ListBox list;
    juce::ComboBox side;
    juce::TextButton earlier { "Earlier" }, later { "Later" }, save { "Save" }, cancel { "Cancel" };
    juce::TextEditor pinName;
    juce::TextButton addButton { "Add pin" }, renameButton { "Rename" }, removeButton { "Remove pin" };
    juce::Label hint;
    juce::Rectangle<int> previewArea;
};

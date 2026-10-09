#pragma once
#include <JuceHeader.h>
#include "CircuitHierarchy.h"
#include <functional>
#include <set>

// Dockable tree of every sheet in the open diagram, fully expanded by default.
// Clicking a sheet opens it through the schematic's own sheet navigation; the
// sheet being viewed is highlighted. The tree follows the schematic by polling
// a cheap signature, so creating, renaming, deleting or loading sub-diagrams
// and navigating by any route keep it current.
class CircuitHierarchyPanel final : public juce::Component, private juce::Timer
{
public:
    struct Source
    {
        std::function<std::vector<circuit_hierarchy::Sheet>()> sheets;
        std::function<juce::String()> rootName;
        std::function<juce::String()> currentSheet;
        std::function<void(const juce::String&)> openSheet;
    };

    explicit CircuitHierarchyPanel(Source source);
    ~CircuitHierarchyPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class SheetItem;

    void timerCallback() override;
    void rebuild(const std::vector<circuit_hierarchy::Row>& rows);

    Source source;
    juce::TreeView tree;
    std::unique_ptr<juce::TreeViewItem> root;
    juce::String signature;
    juce::String current;
    std::set<juce::String> collapsed; // branches the user closed; everything else stays open
};

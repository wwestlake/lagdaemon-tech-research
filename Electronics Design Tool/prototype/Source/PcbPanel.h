#pragma once

#include <JuceHeader.h>

#include "PcbBoard.h"

#include <deque>
#include <functional>
#include <memory>

// The PCB tab. For now: the board itself - pick a standard board, set a
// parametric shape (rectangle, rounded, chamfered, L, U, T, circle, regular
// polygon), or draw any straight-edged outline; add mounting holes and
// cutouts; set layers, thickness and edge clearance. The outline is checked
// as it changes. Footprints, placement and routing build on this.
class PcbPanel final : public juce::Component
{
public:
    PcbPanel();
    ~PcbPanel() override;

    const pcb::BoardDesign& design() const { return board; }
    // Replaces the board (loading a diagram, an agent tool). Not an undo step when `fromFile`.
    void setDesign(const pcb::BoardDesign& design, bool fromFile = false);
    std::function<void()> onChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void undo();
    void zoomToFit();

private:
    class Canvas;
    void rebuildSidebar();
    void edited(const pcb::BoardDesign& next, bool refit = true); // records undo, applies, notifies; refit unless the edit came from the canvas
    void updateInfo();

    pcb::BoardDesign board;
    std::deque<pcb::BoardDesign> undoStack;

    std::unique_ptr<Canvas> canvas;
    juce::Component sidebar;
    juce::Viewport sidebarViewport;
    juce::OwnedArray<juce::Component> sidebarItems;
    juce::Label info, problems;
    bool rebuilding = false;
};

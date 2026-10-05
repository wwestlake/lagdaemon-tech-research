#pragma once

#include <JuceHeader.h>

#include "PcbBoard.h"
#include "PcbLayout.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>

// The PCB tab: the board (a standard board, a parametric shape such as an L
// or a hexagon, or any drawn straight-edged outline, with mounting holes,
// cutouts and stackup), the schematic's parts on it as footprints (placed
// automatically, then dragged and rotated by hand), and the copper routed by
// DjehutiRoute with an exact design-rule check.
class PcbPanel final : public juce::Component
{
public:
    PcbPanel();
    ~PcbPanel() override;

    const pcb::BoardDesign& design() const { return board; }
    // Replaces the board (loading a diagram, an agent tool). Not an undo step when `fromFile`.
    void setDesign(const pcb::BoardDesign& design, bool fromFile = false);
    std::function<void()> onChanged;

    // The schematic's parts and nets (set by the workbench).
    std::function<std::vector<pcb::SchematicPart>()> getSchematicParts;

    const pcb::Layout& layoutState() const { return layout; }
    // Replaces the parts / routing (loading a diagram, an agent tool). Not an undo step when `fromFile`.
    void setLayout(const pcb::Layout& next, bool fromFile = false);
    pcb::SyncReport syncFromSchematic();
    juce::StringArray autoPlace();
    // Routes on a worker thread (the UI stays live); applies the result if the
    // board and parts did not change meanwhile, then calls `finished` on the
    // message thread. `rules` replace the current ones for this route.
    void route(std::optional<pcb::RouteRules> rules = {}, std::function<void()> finished = {});
    bool isRouting() const { return routing; }
    // Compares the board's copper with the schematic as it is now (see pcb::verifyNetlist).
    const pcb::NetlistCheck& verify();
    const pcb::NetlistCheck& lastVerification() const { return verification; }
    void visibilityChanged() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void undo();
    void zoomToFit();

private:
    class Canvas;
    void rebuildSidebar();
    void edited(const pcb::BoardDesign& next, bool refit = true); // records undo, applies, notifies; refit unless the edit came from the canvas
    void editedLayout(const pcb::Layout& next);                    // records undo, applies, notifies
    void selectionChanged();
    void updateInfo();

    struct Snapshot
    {
        pcb::BoardDesign board;
        pcb::Layout layout;
    };
    pcb::BoardDesign board;
    pcb::Layout layout;
    std::deque<Snapshot> undoStack;
    juce::String selectedPart;
    juce::String lastReport;
    pcb::NetlistCheck verification;

    std::unique_ptr<Canvas> canvas;
    juce::Component sidebar;
    juce::Viewport sidebarViewport;
    juce::OwnedArray<juce::Component> sidebarItems;
    juce::Label info, problems, routeInfo, verifyInfo;
    bool rebuilding = false;
    bool routing = false;
};

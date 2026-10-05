#pragma once

#include <JuceHeader.h>

#include "PcbBoard.h"

#include <map>
#include <vector>

// The parts on the board: footprints, the schematic's parts placed as
// footprints with the schematic's nets on their pads, automatic placement,
// and routing / design-rule checking with DjehutiRoute. Saved in the diagram
// file under "pcb_layout" (routed copper included, so a board reopens as it
// was). Millimetres, y up, like the board outline.
namespace pcb
{
// ---- Footprints -------------------------------------------------------------

struct PadDef
{
    juce::String number;    // "1", "2"...
    juce::String pin;       // schematic pin name it carries, "" = not connected
    double x = 0.0, y = 0.0;
    double w = 1.0, h = 1.0;
    bool round = false;     // circle (w) or rectangle (w x h)
    double drill = 0.0;     // > 0: through-hole, on every copper layer
};

struct Footprint
{
    juce::String id, name, description;
    std::vector<PadDef> pads;
    double courtyardW = 2.0, courtyardH = 2.0; // body + pads, centred on the origin
};

const std::vector<Footprint>& footprints();
const Footprint* findFootprint(const juce::String& id);
// Footprints that fit a schematic symbol (their pads name its pins), the default first.
juce::StringArray footprintsFor(const juce::String& symbolId);
juce::String defaultFootprint(const juce::String& symbolId);
// Why a schematic part has no place on a board ("" when it does): instruments
// are test equipment, controlled sources are ideal models.
juce::String notOnBoardReason(const juce::String& symbolId);

// ---- Parts and layout -------------------------------------------------------

// A part as the schematic has it: its pins and the net each is on ("" = unconnected).
struct SchematicPart
{
    juce::String refdes, symbolId, value;
    std::vector<std::pair<juce::String, juce::String>> pins;
};

struct PlacedPart
{
    juce::String refdes, symbolId, value, footprint;
    Point at;               // footprint origin, mm
    int rotation = 0;       // 0, 90, 180, 270 counter-clockwise
    std::map<juce::String, juce::String> pinNets;
};

// A pad where it is on the board.
struct BoardPad
{
    juce::String refdes, number, pin, net;
    Point centre;
    double w = 1.0, h = 1.0; // rotated size
    bool round = false;
    double drill = 0.0;
};

struct RouteRules
{
    double trackWidth = 0.25, clearance = 0.2, viaDiameter = 0.6, viaDrill = 0.3;
};

struct TrackMm
{
    juce::String net;
    int layer = 0;
    double width = 0.25;
    std::vector<Point> points;
};

struct ViaMm
{
    juce::String net;
    Point at;
    double diameter = 0.6, drill = 0.3;
};

struct Marker
{
    juce::String kind, message;
    Point at;
    bool located = false;
};

struct Layout
{
    std::vector<PlacedPart> parts;
    RouteRules rules;

    // Routing result (cleared when anything it depends on changes).
    bool routed = false;
    std::vector<TrackMm> tracks;
    std::vector<ViaMm> vias;
    int connections = 0, routedConnections = 0, iterations = 0;
    double seconds = 0.0;
    juce::StringArray unrouted;   // "R1.2 (net VOUT): reason"
    std::vector<Marker> violations;
    juce::String routeError;

    void clearRoute();
    const PlacedPart* find(const juce::String& refdes) const;
    PlacedPart* find(const juce::String& refdes);

    juce::var toVar() const;
    static Layout fromVar(const juce::var& value);
};

std::vector<BoardPad> padsOf(const PlacedPart& part);
std::vector<BoardPad> allPads(const Layout& layout);
// The part's courtyard on the board (rotated), mm.
juce::Rectangle<double> courtyardOf(const PlacedPart& part);
juce::StringArray netNames(const Layout& layout);

// Placement problems: parts off the board, over holes or cutouts, overlapping.
juce::StringArray placementProblems(const Layout& layout, const BoardDesign& board);

struct SyncReport
{
    juce::StringArray added, removed, updated, skipped; // skipped: "SCOPE1: reason"
};
// Brings the board's parts in line with the schematic: new parts are placed
// in free space, removed parts are taken off, changed nets/values applied.
// Parts already on the board stay where they are. Clears the routing.
SyncReport syncFromSchematic(Layout& layout, const std::vector<SchematicPart>& parts, const BoardDesign& board);

// Places every part again: most-connected first, each where its pads land
// nearest the pads already placed on the same nets, inside the outline, clear
// of holes, cutouts and other parts. Returns parts that did not fit.
juce::StringArray autoPlace(Layout& layout, const BoardDesign& board);
// Places one part in free space next to what it connects to (the others stay).
bool placeOne(Layout& layout, int partIndex, const BoardDesign& board);

// Routes the board with DjehutiRoute and checks the result (exact DRC).
void routeLayout(Layout& layout, const BoardDesign& board);
// Re-checks the stored copper against the current parts and board.
std::vector<Marker> checkLayout(const Layout& layout, const BoardDesign& board);

// Board versus schematic (layout versus schematic). Connectivity is extracted
// from the copper geometry alone - pads, tracks and vias that touch on a
// shared layer are one node, whatever net the router labelled them - then
// each pad is mapped back to its part and pin through the footprint's pin
// map and compared, pin by pin, with the schematic as it is now: missing
// connections (opens), extra connections (shorts), parts missing or extra,
// changed symbols, pins without a pad.
struct NetlistCheck
{
    bool routed = false, matches = false;
    int pinsCompared = 0, schematicNets = 0, boardNodes = 0;
    juce::StringArray problems;
    juce::String summary;
};
NetlistCheck verifyNetlist(const Layout& layout, const BoardDesign& board, const std::vector<SchematicPart>& schematic);
// What the check rests on that geometry cannot prove.
juce::String netlistCheckNote();

// Lines still to be routed: every net's spanning tree when unrouted, else the
// nets with unrouted pads.
std::vector<std::pair<Point, Point>> ratsnest(const Layout& layout);
}

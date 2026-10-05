#pragma once

#include "PcbLayout.h"

#include <djehuti_route/board.h>

// Everything printed or etched on the board besides routed copper: user text
// and graphics, the generated part silkscreen (outlines, reference labels,
// pin-1 marks), solder mask and paste openings. The canvas and the Gerber
// writer both draw from these, so the screen shows what the files contain.
namespace pcb
{
struct ArtStroke
{
    std::vector<Point> points; // polyline, mm
    double width = 0.15;
};

struct Artwork
{
    std::vector<ArtStroke> strokes;
    std::vector<std::vector<Point>> fills; // closed polygons, mm
};

// A pad-shaped opening (mask, paste) or keep-clear area.
struct Opening
{
    Point centre;
    double w = 1.0, h = 1.0;
    bool round = false;
};

const juce::StringArray& artLayers(); // F.SilkS, B.SilkS, F.Cu, B.Cu
bool isBottomLayer(const juce::String& layer);
// Copper layer index for F.Cu / B.Cu, -1 for silkscreen.
int copperLayerIndex(const juce::String& layer, int layerCount);

Artwork textArtwork(const BoardText& text);
Artwork graphicArtwork(const BoardGraphic& graphic);
// User text and graphics on one layer.
Artwork userArtwork(const Layout& layout, const juce::String& layer);

// The silkscreen of one side as printed: part outlines, labels and pin-1
// marks (top only - parts sit on top) plus user silk. Everything under
// `silkClearAreas` is removed by the fab file (clear polarity) and on screen.
Artwork silkscreen(const Layout& layout, bool top);
std::vector<Opening> silkClearAreas(const Layout& layout, bool top);

std::vector<Opening> maskOpenings(const Layout& layout, bool top);
std::vector<Opening> pasteOpenings(const Layout& layout); // top SMD pads

// Copper text and graphics become router keepouts (each stroke and fill
// grown by the clearance), so tracks and vias stay clear of them.
void addCopperArtKeepouts(const Layout& layout, djehuti::route::Board& board);
// Copper art over pads, off the board or too near its edge (blocks routing).
juce::StringArray copperArtProblems(const Layout& layout, const BoardDesign& board);
// Below typical fab minimums (warn only): text under 0.8 mm, lines under 0.15 mm.
juce::StringArray artworkWarnings(const Layout& layout);

juce::Rectangle<double> boundsOf(const Artwork& art);
// Polyline of an arc / circle with chords no further than 0.01 mm from the curve.
std::vector<Point> arcPoints(Point centre, double radius, double startDeg, double endDeg);
}

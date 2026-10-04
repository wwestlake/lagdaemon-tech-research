#pragma once

#include <JuceHeader.h>

#include <vector>

// Generic schematic symbol library: geometry (body bounds, pin offsets) and
// artwork for every supported symbol class. Geometry follows the drawing
// rules in docs/SCHEMATIC_SYMBOL_STANDARDS.md: every pin end sits on the
// 24 px grid relative to the symbol origin (SCH-G2), and symbols rotate in
// 90 degree steps only so pins stay on the grid in every orientation.
namespace schematic
{
constexpr float gridSize = 24.0f;

struct PinDef
{
    juce::String name;
    juce::Point<float> offset;
};

struct SymbolDef
{
    juce::String id;
    juce::String title;
    juce::Rectangle<float> bounds; // body extent in symbol space, unrotated
    std::vector<PinDef> pins;
    bool showPinNames = false;     // only where pin identity is not obvious from the art

    bool isValid() const { return id.isNotEmpty(); }
};

const juce::StringArray& supportedSymbolIds();
bool isSupportedSymbol(const juce::String& symbolId);

// Returns an invalid SymbolDef (empty id) for unknown ids. Callers must
// reject unknown symbols rather than substitute another class.
SymbolDef symbolFor(const juce::String& symbolId);

// IEEE 315 / common EDA reference designator letters (SCH-T3).
juce::String refdesPrefixFor(const juce::String& symbolId);

// Ground, named supply ports and net labels: net markers, not parts.
// Markers with the same name (supply port name, or label name) are one net.
bool isPowerSymbol(const juce::String& symbolId);
bool isInstrumentSymbol(const juce::String& symbolId);
bool isRailBus(const juce::String& symbolId);

int normalizedRotation(int rotation);
juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation);
juce::Rectangle<float> rotateBounds(juce::Rectangle<float> bounds, int rotation);

// Body plus pin ends, unrotated: the full footprint a router must avoid.
// Every pin end lies on this rectangle's boundary.
juce::Rectangle<float> extentBounds(const SymbolDef& symbol);

// Unit direction a wire leaves the pin in, unrotated (away from the body).
// Zero for net markers, which accept a wire from any side.
juce::Point<float> pinLeadDirection(const SymbolDef& symbol, int pinIndex);

// Where the reference designator and value text go for a rotated symbol,
// relative to its origin (SCH-T1, SCH-T2): on a side with no pins, always
// reading left to right. Power symbols have no part labels.
struct LabelRects
{
    juce::Rectangle<float> refdes;
    juce::Rectangle<float> value;
    juce::Justification justification { juce::Justification::centred };
};
LabelRects labelRectsFor(const SymbolDef& symbol, int rotation);

// Sub-diagram block: one pin per port, inputs on the left, outputs on the
// right, spaced two grid steps apart. Its body leaves a band on top for the
// block name. Ports are named; pin names equal port names.
struct BlockPort
{
    juce::String name;
    bool rightSide = false;
};
SymbolDef blockSymbol(const std::vector<BlockPort>& ports);

// The port bubble outline in symbol space (unrotated); the name goes inside.
juce::Rectangle<float> portBubbleRect();

// Draws a sub-diagram block (box, name band, pin names) in symbol space.
void drawBlockArt(juce::Graphics& g, const SymbolDef& block, const juce::String& name);

// Draws the symbol in symbol space; the caller applies rotation and
// translation. `readout` is the live text for symbols that display one
// (the multimeter). Rail buses are drawn by the canvas, not here.
void drawSymbolArt(juce::Graphics& g, const SymbolDef& symbol, const juce::String& readout);
}

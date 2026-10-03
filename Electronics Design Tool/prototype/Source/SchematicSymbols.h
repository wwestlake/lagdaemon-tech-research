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

// Ground and named supply ports: net markers, not parts.
bool isPowerSymbol(const juce::String& symbolId);
bool isInstrumentSymbol(const juce::String& symbolId);
bool isRailBus(const juce::String& symbolId);

int normalizedRotation(int rotation);
juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation);
juce::Rectangle<float> rotateBounds(juce::Rectangle<float> bounds, int rotation);

// Draws the symbol in symbol space; the caller applies rotation and
// translation. `readout` is the live text for symbols that display one
// (the multimeter). Rail buses are drawn by the canvas, not here.
void drawSymbolArt(juce::Graphics& g, const SymbolDef& symbol, const juce::String& readout);
}

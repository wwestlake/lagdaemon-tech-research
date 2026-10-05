#pragma once

#include <JuceHeader.h>

#include <vector>

// The single-stroke vector font for board text: silkscreen and copper text,
// part labels. Gerber has no text, so text goes to the board as strokes; the
// canvas draws the same strokes, so what is shown is what is printed.
// Glyphs sit on a grid: cap height 6 units, x-height 4, descenders to -2,
// every glyph 4 units wide with 1.5 units between letters.
namespace pcb::font
{
using Point = juce::Point<double>;
using Stroke = std::vector<Point>;

// Strokes of `text` in millimetres. `height` is the cap height; `align`
// -1 left, 0 centre, 1 right, about `at`, with the block centred vertically
// on `at`. Rotation in degrees counter-clockwise; `mirror` flips it for the
// bottom side so it reads correctly from underneath. "\n" starts a new line.
std::vector<Stroke> layout(const juce::String& text, Point at, double height, double rotationDeg, int align, bool mirror);

// The glyph strokes of one character in font units (unknown characters draw as '?').
const std::vector<Stroke>& glyph(juce::juce_wchar c);

constexpr double capUnits = 6.0;
constexpr double advanceUnits = 5.5;
constexpr double lineUnits = 10.0;
}

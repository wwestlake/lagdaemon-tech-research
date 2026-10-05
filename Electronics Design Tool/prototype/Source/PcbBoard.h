#pragma once

#include <JuceHeader.h>

#include <djehuti_route/board.h>

#include <map>
#include <vector>

// The board a diagram is laid out on: its outline (from a standard size, a
// parametric shape, or drawn), mounting holes, cutouts, and stackup. Saved in
// the diagram file under "pcb"; the PCB tab and the agent's pcb_board_* tools
// both edit it. Millimetres, y up, the outline's lower-left near (0, 0).
namespace pcb
{
using Point = juce::Point<double>;

struct Hole
{
    Point centre;
    double diameter = 3.2;
};

struct ShapeParam
{
    juce::String key, label;
    double defaultValue = 0.0;
};

struct ShapeSpec
{
    juce::String id, name;
    std::vector<ShapeParam> params;
};

const std::vector<ShapeSpec>& shapes();
const ShapeSpec* findShape(const juce::String& id);

struct BoardDesign
{
    juce::String source = "standard";      // "standard", "shape" or "custom"
    juce::String standardId = "fab-100";
    juce::String shape = "rectangle";
    std::map<juce::String, double> shapeParams;
    std::vector<Point> outline;             // always the current outline
    std::vector<Hole> holes;
    std::vector<std::vector<Point>> cutouts;
    int layers = 2;
    double thickness = 1.6;
    double edgeClearance = 0.3;

    static BoardDesign standard(const juce::String& id);
    static BoardDesign fromShape(const juce::String& shapeId, const std::map<juce::String, double>& params);

    juce::var toVar() const;
    static BoardDesign fromVar(const juce::var& value);

    // Problems that stop it being a board (crossing edges, cutouts off the board, holes over the edge...).
    juce::StringArray problems() const;
    juce::Rectangle<double> bounds() const;
    double areaMm2() const;     // outline less cutouts and holes
    double perimeterMm() const;

    djehuti::route::Board toRouteBoard() const;
};

// Points of a shape at its parameters (missing parameters take their defaults).
std::vector<Point> makeShape(const juce::String& shapeId, const std::map<juce::String, double>& params);
}

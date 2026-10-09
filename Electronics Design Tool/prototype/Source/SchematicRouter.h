#pragma once

#include <JuceHeader.h>

#include <vector>

// Whole-diagram orthogonal wire routing on top of libavoid.
//
// libavoid is used the way it is designed to be used: one Router holds every
// symbol as an obstacle with a connection pin at each pin end, every wire is
// a connector in that same router, and libavoid's orthogonal nudging spaces
// parallel segments of different wires apart. Nets with three or more
// terminals can be routed as hyperedges, letting libavoid choose the
// junction positions (minimum terminal spanning tree plus improvement).
//
// Framework-agnostic geometry in, polylines out; the canvas owns the model.
namespace schematic::routing
{
struct Pin
{
    juce::Point<float> position;   // absolute
    juce::Point<float> direction;  // unit lead direction, away from the body
};

struct Obstacle
{
    juce::Rectangle<float> bounds; // absolute; every pin lies on its boundary
    std::vector<Pin> pins;
};

// A wire end: a pin on an obstacle, a junction, or a free point.
struct Endpoint
{
    int obstacle = -1;
    int pin = -1;
    int junction = -1;
    juce::Point<float> point;      // used only for free points

    static Endpoint forPin(int obstacleIndex, int pinIndex) { Endpoint e; e.obstacle = obstacleIndex; e.pin = pinIndex; return e; }
    static Endpoint forJunction(int index) { Endpoint e; e.junction = index; return e; }
    static Endpoint forPoint(juce::Point<float> p) { Endpoint e; e.point = p; return e; }

    bool isPin() const { return obstacle >= 0 && pin >= 0; }
    bool isJunction() const { return junction >= 0; }
};

struct Connection
{
    Endpoint a;
    Endpoint b;
    int net = -1;                  // connections of the same net may share and touch
    // Pinned routing points, absolute and grid-aligned, visited in this order.
    // Only the stretches between them (and the ends) are routed; the points
    // themselves never move.
    std::vector<juce::Point<float>> waypoints;
};

using Polyline = std::vector<juce::Point<float>>;

struct Style
{
    double segmentPenalty = 50.0; // higher: fewer bends, possibly longer wires
    float wireGapGrids = 1.0f;    // spacing between parallel wires of different nets
};

struct Problem
{
    std::vector<Obstacle> obstacles;
    std::vector<juce::Point<float>> junctions; // fixed positions
    std::vector<Connection> connections;
};

// Routes every connection together. Result has one polyline per connection
// (empty if it could not be routed legally), snapped to the schematic grid.
// Junctions are slid along shared runs to where their wires actually part;
// pass adjustedJunctions to receive those positions (same order).
// A connection fails - empty polyline, reason in failures[i] - when libavoid
// finds no path, a pinned waypoint is not visited in order, or the route would
// pass through a component body. Callers keep their previous route then.
std::vector<Polyline> routeConnections(const Problem& problem, float gridSize,
                                       std::vector<juce::Point<float>>* adjustedJunctions = nullptr, const Style& style = {},
                                       std::vector<juce::String>* failures = nullptr);

// Checks used by routeConnections, exposed for tests.
bool visitsInOrder(const Polyline& route, const std::vector<juce::Point<float>>& points);
bool crossesBody(const Polyline& route, const std::vector<Obstacle>& obstacles);
// True when the two routes run along the same line for more than a point.
bool routesOverlap(const Polyline& a, const Polyline& b);

// One net to be routed as a tree.
struct NetTerminals
{
    std::vector<Endpoint> terminals; // pins only
};

struct TreeEdge
{
    Endpoint a;                      // pin, or junction index into TreeSolution::junctions
    Endpoint b;
};

struct TreeSolution
{
    std::vector<juce::Point<float>> junctions; // grid-snapped branch points
    std::vector<TreeEdge> edges;
};

// Lets libavoid decide the wiring topology of each net: two-terminal nets
// become one edge; larger nets become hyperedge trees with junctions at the
// branch points. Junctions left with fewer than three edges are dissolved.
std::vector<TreeSolution> routeNetTrees(const std::vector<Obstacle>& obstacles,
                                        const std::vector<NetTerminals>& nets,
                                        float gridSize, const Style& style = {});
}

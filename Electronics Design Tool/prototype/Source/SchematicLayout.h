#pragma once

#include "SchematicSymbols.h"

#include <vector>

// Connectivity-driven schematic placement (docs/SCHEMATIC_SYMBOL_STANDARDS.md,
// SCH-F1..F5, SCH-P1..P3).
//
// Signal flows left to right in layers found by walking the circuit from
// its input sources. Each part lines its input pin up with the net that
// feeds it, so the main signal path runs straight. Two-pin parts that hang
// between a signal net and ground or a supply ("shunts") stand vertically
// one grid step off that net, power side down for ground and negative
// supplies, up for positive supplies. DC supply sources go into a separate
// supply block, instruments into a column on the far right, and every pin
// on ground or a supply gets its own ground symbol or named supply port
// instead of a long wire. Instrument inputs connect through matching net
// labels (one at the instrument, one at the probed net) rather than wires
// across the drawing. A two-pin part fed from a vertical pin (an emitter,
// a collector) stacks in line with that pin instead of starting a column.
//
// Pure geometry: the canvas converts its model in and applies the result.
namespace schematic::layout
{
enum class NetKind { Signal, Ground, Supply };

struct Net
{
    juce::String name;  // supply nets: the port name, e.g. "+12V"
    NetKind kind = NetKind::Signal;
};

struct Part
{
    juce::String refdes;                 // names instrument probe labels
    SymbolDef symbol;
    std::vector<int> pinNets;            // net index per pin, -1 if unconnected
    juce::Point<float> originalPosition; // tie-breaks only
};

// A net marker placed by the layout. Most attach to one part pin with a
// short wire (part/pin set). Circuit-side probe labels instead join their
// net's wiring tree (onNet with net set, part/pin -1).
struct Marker
{
    int part = -1;
    int pin = -1;
    juce::String symbolId;               // "ground", "power_port" or "net_label"
    juce::String netName;                // supply/label name
    juce::Point<float> position;
    int rotation = 0;
    bool onNet = false;
    int net = -1;
};

struct Result
{
    std::vector<juce::Point<float>> positions;
    std::vector<int> rotations;
    std::vector<Marker> markers;
};

Result layoutSchematic(const std::vector<Part>& parts, const std::vector<Net>& nets, float gridSize);
}

#pragma once

#include <JuceHeader.h>

#include "CircuitSolver.h"
#include "SignalMeasure.h"

#include <complex>
#include <map>
#include <vector>

// SPICE analytics: runs every analysis on a circuit and turns the solver's
// numbers into plots and tables. The Analytics window and the agent's
// analytics_* tools both go through run(), with the same settings keys, so
// whatever one can do the other can too. Knows nothing about schematics:
// the host supplies the circuit with its net and part names.
namespace analytics
{
enum class Analysis
{
    OperatingPoint, DcSweep, Ac, Transient, Fourier, Noise,
    TransferFunction, Sensitivity, PoleZero, Temperature, MonteCarlo
};

struct AnalysisInfo
{
    Analysis id;
    juce::String key;         // tool/settings id: "operating_point"
    juce::String title;       // "Operating Point (.OP)"
    juce::String description;
};

const std::vector<AnalysisInfo>& analyses();
const AnalysisInfo* findAnalysis(const juce::String& key);
const AnalysisInfo& infoFor(Analysis analysis);

enum class FieldKind
{
    Quantity,  // engineering number (or "auto" where the help says so)
    Integer,
    Choice,    // one of options
    Net,       // a net name
    Nets,      // comma-separated net names and I(part) currents; empty = every net
    Source,    // an independent source's reference designator
    Target,    // "R1.value", "V1.dc", ... or "TEMP"; "None" where optional
    Text
};

struct Field
{
    juce::String key;
    juce::String label;
    FieldKind kind = FieldKind::Text;
    juce::String unit;
    juce::String defaultValue;
    juce::String help;
    juce::StringArray options;
    juce::String group;       // "Setup", "Outputs", "Step", "Tolerances"
};

std::vector<Field> fieldsFor(Analysis analysis);

using Settings = std::map<juce::String, juce::String>;

struct NetInfo
{
    juce::String name;  // label name, or the net id
    int node = 0;
    juce::String pins;  // "R1.2 C1.1 Q1.B"
};

struct PartInfo
{
    juce::String refdes;
    juce::String symbolId;
    int element = -1;   // primary solver element
};

struct Netlist
{
    circuit_sim::Circuit circuit;
    std::vector<NetInfo> nets;
    std::vector<PartInfo> parts;
    juce::StringArray warnings;
};

// Choices for Net / Source / Target fields.
juce::StringArray netChoices(const Netlist& netlist);    // "GND" first
juce::StringArray sourceChoices(const Netlist& netlist);
juce::StringArray targetChoices(const Netlist& netlist, bool includeTemperature);

struct Trace
{
    juce::String name;
    juce::String unit;
    std::vector<double> x, y;
    int partnerPlot = -1, partnerTrace = -1; // AC magnitude <-> phase
};

struct Plot
{
    enum class Kind { Lines, Bars, PoleZero, Histogram };
    Kind kind = Kind::Lines;
    juce::String title, xLabel, xUnit, yLabel, yUnit;
    bool logX = false, logY = false;
    std::vector<Trace> traces;
    juce::StringArray categories;                   // Bars: one label per x index (x = 0, 1, 2...)
    std::vector<std::complex<double>> poles, zeros; // PoleZero (rad/s)
};

struct Table
{
    juce::String title;
    juce::StringArray columns;
    std::vector<juce::StringArray> rows;
};

struct Result
{
    bool ok = false;
    Analysis analysis = Analysis::OperatingPoint;
    juce::String title;
    juce::String error;
    juce::String summary;
    std::vector<Plot> plots;
    std::vector<Table> tables;
    juce::StringArray warnings;
    Settings settings;
    double seconds = 0.0;
    juce::Time when;
};

Result run(Analysis analysis, const Settings& settings, const Netlist& netlist);

// A trace by name across all plots, or nullptr.
const Trace* findTrace(const Result& result, const juce::String& name, int* plotIndex = nullptr);
juce::StringArray traceNames(const Result& result);
signal_measure::Result measure(const Result& result, const juce::String& traceName, const signal_measure::Request& request);

juce::String toCsv(const Plot& plot);
juce::String toCsv(const Table& table);
juce::String toJson(const Result& result, const juce::StringArray& files);

juce::String formatNumber(double value, const juce::String& unit, int significant = 4);
}

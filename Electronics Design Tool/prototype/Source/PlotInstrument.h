#pragma once

#include <JuceHeader.h>

#include "Analytics.h"

#include <vector>

// The 2D/3D plotting instrument without its window: which circuit quantities
// its probes measure, how the transient samples of those quantities are put
// on one common time base, and how a large trajectory is thinned for display.
// The schematic supplies the probe hookups (the nets on each channel's + and -
// pins); the window draws what acquire() returns. Nothing here changes the
// circuit: probes add no element to the netlist.
namespace plot_instrument
{
enum class Mode { Time, XY, XYZ };

juce::String modeName(Mode mode);              // "Time", "XY", "XYZ"
Mode parseMode(const juce::String& text);      // unknown text: XY

// One probe channel (A, B, C): the voltage of its + pin's net minus its -
// pin's net. Nodes are solver nodes; 0 is ground, -1 a pin with no wire.
struct Channel
{
    juce::String name;
    int plusNode = -1;
    int minusNode = -1;
};

// What a signal spec may be: a channel letter ("A"), a net voltage "V(out)",
// a differential net voltage "V(a,b)", or a branch current "I(R1)".
struct SignalSpec
{
    juce::String spec;       // as the user wrote it
    juce::String label;      // "A: V(out)-V(n2)", "I(R1)"
    juce::String unit;       // "V" or "A"
    juce::String plusTrace;  // analytics trace name, e.g. "V(out)"; empty = 0
    juce::String minusTrace; // empty = single ended
    juce::String problem;    // why it cannot be measured; empty when it can
};

struct Plan
{
    std::vector<SignalSpec> signals; // in axis order: X, Y, Z (or the time traces)
    juce::String outputs;            // analytics "outputs" setting for the run
    juce::StringArray problems;      // every unmeasurable signal, readable
    bool canRun() const;             // at least one signal (all of them for XY/XYZ)
    Mode mode = Mode::XY;
};

// Resolves each signal spec against the netlist and the probe channels.
Plan plan(const analytics::Netlist& netlist, const std::vector<Channel>& channels,
          Mode mode, const juce::StringArray& signalSpecs);

// One series of samples: values over its own time base. Pointers, so the
// analysis result is not copied.
struct Series
{
    const std::vector<double>* time = nullptr;
    const std::vector<double>* values = nullptr;
};

// Signals on one common time base. values[signal][sample]; a NaN marks a
// sample that has no finite value in some signal (the trace breaks there).
struct Acquisition
{
    bool ok = false;
    juce::String error;
    juce::StringArray warnings;
    std::vector<double> time;
    std::vector<std::vector<double>> values;
    std::vector<juce::String> labels, units;
    bool resampled = false; // the signals had different time bases
    int gaps = 0;           // samples with a non-finite value
    size_t size() const { return time.size(); }
};

// The synchronisation rule. When every series has the same time base (one
// transient run), each output sample takes the values of the same solver
// step, unchanged: no interpolation. When time bases differ, the common time
// base is the sorted union of all sample times inside the interval every
// series covers, and each series is linearly interpolated onto it (at a
// repeated time, a discontinuity, the earlier sample is used). Differential
// signals are subtracted after synchronisation. Fails on empty series, size
// mismatches, or time running backwards.
struct SignalSeries
{
    juce::String label, unit;
    Series plus;
    Series minus; // values == nullptr: single ended
};
Acquisition synchronise(const std::vector<SignalSeries>& signals);

// Looks the plan's traces up in a transient result and synchronises them.
Acquisition acquire(const analytics::Result& result, const Plan& plan);

// Display thinning: sample indices, in time order, to draw when there are
// more than maxPoints. Deterministic: the samples are cut into equal buckets
// and each bucket keeps its first sample, the minimum and maximum of every
// listed signal, and its first gap (NaN) so breaks stay visible. The last
// sample is always kept. The acquisition itself is never changed.
std::vector<int> displayIndices(const Acquisition& acq, const std::vector<int>& signalsToKeep, int maxPoints);

struct Range
{
    double min = 0.0, max = 1.0;
    bool valid = false;
};
// Finite min/max over all samples, padded 5%; a flat signal gets +-1 (or +-10%).
Range autoRange(const std::vector<double>& values);
// "auto" or empty keeps the automatic bound; otherwise an engineering number.
Range applyManual(Range automatic, const juce::String& minText, const juce::String& maxText);

// The 3D view, kept apart from the data. Angles in degrees.
struct Camera
{
    double yaw = -35.0, pitch = 25.0, zoom = 1.0, panX = 0.0, panY = 0.0;
    bool perspective = true;
};
juce::String toString(const Camera& camera);   // "yaw=-35 pitch=25 zoom=1 panx=0 pany=0"
Camera parseCamera(const juce::String& text);  // missing keys keep the defaults

// A point in normalised plot space (each axis -1..1) to the screen area.
// depth grows away from the viewer.
struct Projected
{
    float x = 0.0f, y = 0.0f;
    double depth = 0.0;
};
Projected project(const Camera& camera, double x, double y, double z, juce::Rectangle<float> area);

// Round tick values (1, 2, 5 x 10^n steps) inside [min, max], about `count` of them.
std::vector<double> niceTicks(double min, double max, int count);

// Engineering number: 4.7k, 10u, 2meg, 1e-3; unit letters after it are ignored.
double parseNumber(const juce::String& text, double fallback, bool* ok = nullptr);
}

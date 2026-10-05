#pragma once

// Measurements on sampled traces (the SPICE .MEAS set), framework-agnostic.
// A trace is y(x): x is time for transient results, frequency for AC/noise,
// the swept value for DC sweeps. Nothing here knows about circuits or UI.

#include <string>
#include <vector>

namespace signal_measure
{
enum class Kind
{
    Minimum, Maximum, PeakToPeak, Average, Rms, Integral,
    ValueAt,            // y at x = `at`
    WhenCrosses,        // x where y crosses `level` (nth crossing, edge)
    RiseTime,           // lowPercent -> highPercent of the initial->final step
    FallTime,
    OvershootPercent,   // beyond the final value, % of the step
    SettlingTime,       // last time outside final +/- bandPercent of the step, from the start
    Frequency,          // from rising crossings of the mid level
    Period,
    PeakX,              // x at the maximum
    Bandwidth3dB,       // y in dB: width of the band within 3 dB of the peak
    LowerCorner3dB,     // y in dB: lower -3 dB frequency (0 when the band reaches the start)
    UpperCorner3dB,
    UnityGainFrequency, // y in dB: where y crosses 0 dB going down
    PhaseMargin,        // y in dB plus phase: 180 + phase at unity gain, degrees
    GainMargin,         // y in dB plus phase: -gain where phase crosses -180, dB
};

enum class Edge { Rising, Falling, Either };

struct Request
{
    Kind kind = Kind::Maximum;
    double from = -1e300, to = 1e300; // x window
    double at = 0.0;
    double level = 0.0;
    int nth = 1;
    Edge edge = Edge::Rising;
    double lowPercent = 10.0, highPercent = 90.0, bandPercent = 2.0;
};

struct Result
{
    bool ok = false;
    double value = 0.0;
    std::string error;
};

// `phase` (degrees, parallel to y) is needed only for PhaseMargin and GainMargin.
Result measure(const Request& request, const std::vector<double>& x, const std::vector<double>& y,
               const std::vector<double>& phase = {});

const char* kindName(Kind kind);
std::vector<Kind> allKinds();
}

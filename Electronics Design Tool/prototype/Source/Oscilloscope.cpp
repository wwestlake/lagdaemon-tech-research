#include "Oscilloscope.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace oscilloscope
{
namespace
{
signal_measure::Result unavailable(const std::string& reason) { return { false, 0.0, reason }; }
signal_measure::Result valid(double value) { return { true, value, {} }; }
double crossing(double ta, double a, double tb, double b, double level)
{ return ta + (level - a) / (b - a) * (tb - ta); }
std::vector<double> crossings(const std::vector<double>& t, const std::vector<double>& v, double level, bool rising)
{
    std::vector<double> result;
    for (size_t i = 1; i < t.size(); ++i)
        if (rising ? (v[i - 1] < level && v[i] >= level) : (v[i - 1] > level && v[i] <= level))
            result.push_back(crossing(t[i - 1], v[i - 1], t[i], v[i], level));
    return result;
}
// First complete 10-90% transition, with a fresh low crossing after a reversal.
signal_measure::Result edge(const std::vector<double>& t, const std::vector<double>& v, double lo, double hi, bool rising)
{
    double begin = -1.0;
    for (size_t i = 1; i < t.size(); ++i)
    {
        const auto a = v[i - 1], b = v[i];
        if (rising ? (a < lo && b >= lo) : (a > hi && b <= hi))
            begin = crossing(t[i - 1], a, t[i], b, rising ? lo : hi);
        if (begin >= 0.0 && (rising ? (a < hi && b >= hi) : (a > lo && b <= lo)))
            return valid(crossing(t[i - 1], a, t[i], b, rising ? hi : lo) - begin);
        if (rising ? b < lo : b > hi) begin = -1.0;
    }
    return unavailable("No complete 10-90% transition in the visible record.");
}
double niceScale(double value)
{
    if (!(value > 0.0) || !std::isfinite(value)) return 1.0;
    const auto power = std::pow(10.0, std::floor(std::log10(value)));
    for (double n : { 1.0, 2.0, 5.0, 10.0 }) if (n * power >= value) return n * power;
    return power * 10.0;
}
}

Measurements measure(const std::vector<double>& time, const std::vector<double>& volts, double from, double to)
{
    Measurements m;
    auto get = [&](signal_measure::Kind kind) {
        signal_measure::Request r; r.kind = kind; r.from = from; r.to = to;
        return signal_measure::measure(r, time, volts);
    };
    using K = signal_measure::Kind;
    m.minimum = get(K::Minimum); m.maximum = get(K::Maximum); m.mean = get(K::Average);
    m.peakToPeak = get(K::PeakToPeak); m.rms = get(K::Rms);
    m.frequency = get(K::Frequency); m.period = get(K::Period);
    m.riseTime = m.fallTime = m.pulseWidth = m.dutyPercent = unavailable("Insufficient waveform excursion or complete edges.");
    if (time.size() != volts.size() || !m.peakToPeak.ok || m.peakToPeak.value < 1e-12) return m;
    std::vector<double> t, v;
    for (size_t i = 0; i < time.size(); ++i) if (time[i] >= from && time[i] <= to)
    { t.push_back(time[i]); v.push_back(volts[i]); }
    if (t.size() < 2) return m;
    const auto minimum = m.minimum.value, span = m.peakToPeak.value;
    m.riseTime = edge(t, v, minimum + 0.1 * span, minimum + 0.9 * span, true);
    m.fallTime = edge(t, v, minimum + 0.1 * span, minimum + 0.9 * span, false);
    const auto up = crossings(t, v, minimum + 0.5 * span, true);
    const auto down = crossings(t, v, minimum + 0.5 * span, false);
    if (!up.empty())
    {
        const auto falling = std::upper_bound(down.begin(), down.end(), up.front());
        if (falling != down.end())
        {
            m.pulseWidth = valid(*falling - up.front());
            if (up.size() >= 2 && *falling < up[1]) m.dutyPercent = valid(100.0 * m.pulseWidth.value / (up[1] - up[0]));
        }
    }
    double dt = 0.0;
    for (size_t i = 1; i < t.size(); ++i) dt = std::max(dt, t[i] - t[i - 1]);
    m.edgeResolutionLimited = (m.riseTime.ok && m.riseTime.value < 2 * dt) || (m.fallTime.ok && m.fallTime.value < 2 * dt);
    return m;
}

Frame prepare(const Record& r, const Settings& s)
{
    Frame f;
    if (!r.ok) { f.error = r.error; return f; }
    if (r.time.size() < 2) { f.error = "The capture has fewer than two samples."; return f; }
    for (size_t i = 0; i < r.time.size(); ++i)
    {
        if (!std::isfinite(r.time[i]) || (i > 0 && r.time[i] <= r.time[i - 1]))
        { f.error = "Capture times must be finite and strictly increasing."; return f; }
        if (i > 0) f.sampleInterval = std::max(f.sampleInterval, r.time[i] - r.time[i - 1]);
    }
    if (!(s.timePerDiv > 0.0) || !std::isfinite(s.timePerDiv) || !std::isfinite(s.horizontalPosition))
    { f.error = "Enter a positive time/div and finite horizontal position."; return f; }
    for (size_t ch = 0; ch < 2; ++ch)
    {
        if (!r.connected[ch]) continue;
        const auto& c = s.channel[ch];
        if (r.channel[ch].size() != r.time.size() || !(c.voltsPerDiv > 0.0) || !(c.scale > 0.0))
        { f.error = "Invalid channel samples or scale."; return f; }
        for (double v : r.channel[ch]) if (!std::isfinite(v))
        { f.error = "A capture contains nonfinite voltage samples."; return f; }
        signal_measure::Request mean; mean.kind = signal_measure::Kind::Average;
        f.rawMean[ch] = signal_measure::measure(mean, r.time, r.channel[ch]).value * c.scale;
        f.values[ch].reserve(r.time.size());
        for (double v : r.channel[ch]) f.values[ch].push_back(v * c.scale - (c.acCoupled ? f.rawMean[ch] : 0.0));
    }
    const auto width = s.timePerDiv * 10.0;
    const auto triggerCh = (size_t)std::clamp(s.triggerChannel, 0, 1);
    if (r.connected[triggerCh])
    {
        const auto edges = crossings(r.time, f.values[triggerCh], s.triggerLevel, s.rising);
        // Prefer an edge with enough real pre/post-trigger samples.
        for (double t : edges)
            if (t - s.triggerPosition * width >= r.time.front() && t + (1 - s.triggerPosition) * width <= r.time.back())
            { f.triggered = true; f.triggerTime = t; break; }
        if (!f.triggered && !edges.empty()) { f.triggered = true; f.triggerTime = edges.front(); }
    }
    f.waiting = s.normal && !f.triggered;
    const auto base = f.triggered ? f.triggerTime - s.triggerPosition * width : r.time.front();
    f.start = std::clamp(base + s.horizontalPosition, r.time.front(), std::max(r.time.front(), r.time.back() - width));
    f.stop = f.start + width;
    if (s.math != 0 && r.connected[0] && r.connected[1])
        for (size_t i = 0; i < r.time.size(); ++i)
            f.mathValues.push_back(s.mathScale * (f.values[0][i] + (s.math == 1 ? f.values[1][i] : -f.values[1][i])));
    for (size_t ch = 0; ch < 2; ++ch)
        if (r.connected[ch]) f.measurements[ch] = measure(r.time, f.values[ch], f.start, f.stop);
    f.ok = true;
    return f;
}

Settings autoscale(const Record& r, Settings s, bool fitRecord)
{
    if (!r.ok || r.time.size() < 2) return s;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        if (!r.connected[ch] || r.channel[ch].empty()) continue;
        s.channel[ch].enabled = true;
        const auto [a, b] = std::minmax_element(r.channel[ch].begin(), r.channel[ch].end());
        auto& c = s.channel[ch];
        c.voltsPerDiv = niceScale(std::max(1e-6, (*b - *a) * c.scale) / 6.0);
        c.position = 0;
        signal_measure::Request q; q.kind = signal_measure::Kind::Average;
        const auto mean = signal_measure::measure(q, r.time, r.channel[ch]).value * c.scale;
        c.offset = 0.5 * (*a + *b) * c.scale - (c.acCoupled ? mean : 0.0);
    }
    const auto ch = r.connected[0] ? 0 : 1;
    const auto m = measure(r.time, r.channel[(size_t)ch], r.time.front(), r.time.back());
    s.timePerDiv = fitRecord || !m.period.ok ? (r.time.back() - r.time.front()) / 10.0 : m.period.value * 3.0 / 10.0;
    s.horizontalPosition = 0;
    return s;
}

double divisions(double v, const Channel& c) { return (v - c.offset) / c.voltsPerDiv + c.position; }
double voltsAtDivisions(double d, const Channel& c) { return (d - c.position) * c.voltsPerDiv + c.offset; }
double cursorFrequency(const Settings& s) { return s.t1 == s.t2 ? 0.0 : 1.0 / std::abs(s.t2 - s.t1); }

std::vector<size_t> envelope(const std::vector<double>& t, const std::vector<double>& v, double from, double to, int pixels)
{
    std::vector<size_t> out;
    if (t.size() != v.size() || t.empty() || pixels < 1 || to <= from) return out;
    auto i = (size_t)(std::lower_bound(t.begin(), t.end(), from) - t.begin());
    if (i > 0) --i;
    while (i < t.size() && t[i] <= to)
    {
        const int bucket = (int)((t[i] - from) / (to - from) * pixels);
        size_t lo = i, hi = i, end = i + 1;
        while (end < t.size() && t[end] <= to && (int)((t[end] - from) / (to - from) * pixels) == bucket)
        { if (v[end] < v[lo]) lo = end; if (v[end] > v[hi]) hi = end; ++end; }
        out.push_back(std::min(lo, hi));
        if (lo != hi) out.push_back(std::max(lo, hi));
        i = end;
    }
    if (i < t.size()) out.push_back(i);
    return out;
}
}

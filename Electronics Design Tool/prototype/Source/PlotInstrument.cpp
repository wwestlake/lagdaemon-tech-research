#include "PlotInstrument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace plot_instrument
{
namespace
{
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

// The net name the analytics outputs use for a solver node; empty if the
// node is not in the simulated circuit.
juce::String netName(const analytics::Netlist& n, int node)
{
    for (const auto& net : n.nets)
        if (net.node == node)
            return net.name;
    return {};
}

// "GND"/"0" is ground (node 0); otherwise the net with that name, or -1.
int nodeNamed(const analytics::Netlist& n, juce::String name)
{
    name = name.trim();
    if (name.equalsIgnoreCase("GND") || name == "0")
        return 0;
    for (const auto& net : n.nets)
        if (net.name.equalsIgnoreCase(name))
            return net.node;
    return -1;
}

juce::String partNamed(const analytics::Netlist& n, const juce::String& name)
{
    for (const auto& p : n.parts)
        if (p.refdes.equalsIgnoreCase(name.trim()))
            return p.refdes;
    return n.circuit.find(name.trim().toStdString()) >= 0 ? name.trim() : juce::String();
}

// A voltage between two solver nodes as traces; fills the problem otherwise.
void voltageBetween(const analytics::Netlist& n, int plus, int minus, SignalSpec& s)
{
    s.unit = "V";
    const auto plusName = plus > 0 ? netName(n, plus) : juce::String();
    const auto minusName = minus > 0 ? netName(n, minus) : juce::String();
    if (plus > 0 && plusName.isEmpty())
        s.problem = "the + side's net is not part of the simulated circuit";
    else if (minus > 0 && minusName.isEmpty())
        s.problem = "the - side's net is not part of the simulated circuit";
    else if (plus == minus)
        s.problem = "both sides are on the same net, so it always reads 0";
    if (s.problem.isNotEmpty())
        return;
    s.plusTrace = plus > 0 ? "V(" + plusName + ")" : juce::String();
    s.minusTrace = minus > 0 ? "V(" + minusName + ")" : juce::String();
}

juce::String voltageText(const SignalSpec& s)
{
    if (s.minusTrace.isEmpty())
        return s.plusTrace.isEmpty() ? juce::String("0") : s.plusTrace;
    return (s.plusTrace.isEmpty() ? juce::String("0") : s.plusTrace) + "-" + s.minusTrace;
}

SignalSpec resolve(const analytics::Netlist& n, const std::vector<Channel>& channels, const juce::String& text)
{
    SignalSpec s;
    s.spec = text.trim();
    s.label = s.spec;
    const auto upper = s.spec.toUpperCase();
    if (s.spec.isEmpty())
    {
        s.problem = "no signal is assigned";
        return s;
    }
    for (const auto& c : channels)
        if (upper == c.name.toUpperCase())
        {
            s.label = c.name;
            if (c.plusNode < 0)
            {
                s.problem = "channel " + c.name + " is not connected; wire " + c.name + "+ to the net to measure";
                return s;
            }
            // An unwired - pin measures against ground.
            voltageBetween(n, c.plusNode, std::max(0, c.minusNode), s);
            if (s.problem.isNotEmpty())
                s.problem = "channel " + c.name + ": " + s.problem;
            else
                s.label = c.name + ": " + voltageText(s);
            return s;
        }
    if (upper.startsWith("V(") && upper.endsWith(")"))
    {
        const auto inner = s.spec.substring(2, s.spec.length() - 1);
        auto names = juce::StringArray::fromTokens(inner, ",", "");
        names.trim();
        if (names.size() < 1 || names.size() > 2 || names[0].isEmpty())
        {
            s.problem = "write V(net) or V(net1,net2)";
            return s;
        }
        const auto plus = nodeNamed(n, names[0]);
        const auto minus = names.size() == 2 ? nodeNamed(n, names[1]) : 0;
        if (plus < 0 || minus < 0)
        {
            const auto missing = plus < 0 ? names[0] : names[1];
            s.problem = "no net named " + missing;
            // A pin label (C1.1) where a net name belongs: say which net that pin is on.
            for (const auto& net : n.nets)
                if (juce::StringArray::fromTokens(net.pins, " ", "").contains(missing, true))
                {
                    s.problem << "; " << missing << " is a pin, not a net. It is on net " << net.name << ": write V(" << net.name << ")";
                    break;
                }
            return s;
        }
        voltageBetween(n, plus, minus, s);
        if (s.problem.isEmpty())
            s.label = voltageText(s);
        return s;
    }
    if (upper.startsWith("I(") && upper.endsWith(")"))
    {
        const auto part = partNamed(n, s.spec.substring(2, s.spec.length() - 1));
        if (part.isEmpty())
        {
            s.problem = "no part " + s.spec.substring(2, s.spec.length() - 1).trim() + " carries a branch current the simulator reports";
            return s;
        }
        s.unit = "A";
        s.plusTrace = "I(" + part + ")";
        s.label = s.plusTrace;
        return s;
    }
    s.problem = "unknown signal; use a channel (A, B, C), V(net), V(net1,net2) or I(part)";
    return s;
}

bool sameTimeBase(const std::vector<double>& a, const std::vector<double>& b)
{
    return &a == &b || a == b;
}

// Value of a series at time t (t inside its span); see synchronise().
double valueAt(const Series& s, double t)
{
    const auto& time = *s.time;
    const auto& values = *s.values;
    const auto it = std::lower_bound(time.begin(), time.end(), t);
    if (it == time.end())
        return values.back();
    const auto i = (size_t)(it - time.begin());
    if (*it == t || i == 0)
        return values[i];
    const auto t0 = time[i - 1], t1 = time[i];
    const auto f = (t - t0) / (t1 - t0);
    return values[i - 1] + (values[i] - values[i - 1]) * f;
}

juce::String checkSeries(const Series& s, const juce::String& label)
{
    if (s.time == nullptr || s.values == nullptr)
        return label + " has no data";
    if (s.time->empty())
        return label + " has no samples (empty or interrupted run)";
    if (s.time->size() != s.values->size())
        return label + " has " + juce::String((int)s.time->size()) + " times but " + juce::String((int)s.values->size()) + " values";
    for (size_t i = 1; i < s.time->size(); ++i)
        if (!((*s.time)[i] >= (*s.time)[i - 1]))
            return label + "'s time runs backwards at sample " + juce::String((int)i);
    return {};
}
}

juce::String modeName(Mode mode)
{
    return mode == Mode::Time ? "Time" : mode == Mode::XYZ ? "XYZ" : "XY";
}

Mode parseMode(const juce::String& text)
{
    const auto t = text.trim();
    if (t.equalsIgnoreCase("Time")) return Mode::Time;
    if (t.equalsIgnoreCase("XYZ")) return Mode::XYZ;
    return Mode::XY;
}

bool Plan::canRun() const
{
    int good = 0;
    for (const auto& s : signals)
        if (s.problem.isEmpty())
            ++good;
    if (mode == Mode::Time)
        return good > 0;
    return good == (int)signals.size() && (int)signals.size() == (mode == Mode::XYZ ? 3 : 2);
}

Plan plan(const analytics::Netlist& netlist, const std::vector<Channel>& channels, Mode mode, const juce::StringArray& specs)
{
    Plan p;
    p.mode = mode;
    static const char* axisNames[] = { "X", "Y", "Z" };
    const auto wanted = mode == Mode::XYZ ? 3 : mode == Mode::XY ? 2 : specs.size();
    juce::StringArray outputs;
    for (int i = 0; i < wanted; ++i)
    {
        auto s = resolve(netlist, channels, specs[i]);
        const auto role = mode == Mode::Time ? "Trace " + juce::String(i + 1) : juce::String(axisNames[i]) + " axis";
        if (s.problem.isNotEmpty())
            p.problems.add(role + " (" + (s.spec.isEmpty() ? juce::String("unset") : s.spec) + "): " + s.problem + ".");
        else
            for (const auto& trace : { s.plusTrace, s.minusTrace })
                if (trace.isNotEmpty())
                    outputs.addIfNotAlreadyThere(trace, true);
        p.signals.push_back(std::move(s));
    }
    if (mode == Mode::Time && wanted == 0)
        p.problems.add("No time traces are assigned.");
    p.outputs = outputs.joinIntoString(", ");
    return p;
}

Acquisition synchronise(const std::vector<SignalSeries>& signals)
{
    Acquisition a;
    if (signals.empty())
    {
        a.error = "No signals to acquire.";
        return a;
    }
    std::vector<const Series*> all;
    for (const auto& s : signals)
    {
        for (const auto* series : { &s.plus, &s.minus })
        {
            if (series == &s.minus && s.minus.values == nullptr)
                continue;
            if (const auto problem = checkSeries(*series, s.label); problem.isNotEmpty())
            {
                a.error = problem + ".";
                return a;
            }
            all.push_back(series);
        }
        a.labels.push_back(s.label);
        a.units.push_back(s.unit);
    }

    bool shared = true;
    for (const auto* s : all)
        shared = shared && sameTimeBase(*s->time, *all.front()->time);

    if (shared)
    {
        a.time = *all.front()->time;
    }
    else
    {
        double lo = -std::numeric_limits<double>::infinity(), hi = std::numeric_limits<double>::infinity();
        for (const auto* s : all)
        {
            lo = std::max(lo, s->time->front());
            hi = std::min(hi, s->time->back());
        }
        if (!(lo <= hi))
        {
            a.error = "The signals' time ranges do not overlap, so no sample is simultaneous.";
            return a;
        }
        for (const auto* s : all)
            for (auto t : *s->time)
                if (t >= lo && t <= hi)
                    a.time.push_back(t);
        std::sort(a.time.begin(), a.time.end());
        a.time.erase(std::unique(a.time.begin(), a.time.end()), a.time.end());
        a.resampled = true;
        a.warnings.add("The signals had different time bases; each was linearly interpolated onto "
                       + juce::String((int)a.time.size()) + " common samples.");
    }

    a.values.resize(signals.size());
    for (size_t k = 0; k < signals.size(); ++k)
    {
        auto& out = a.values[k];
        out.resize(a.time.size());
        const auto& s = signals[k];
        for (size_t i = 0; i < a.time.size(); ++i)
        {
            const auto plus = shared ? (*s.plus.values)[i] : valueAt(s.plus, a.time[i]);
            const auto minus = s.minus.values == nullptr ? 0.0 : shared ? (*s.minus.values)[i] : valueAt(s.minus, a.time[i]);
            const auto v = plus - minus;
            out[i] = std::isfinite(v) ? v : nan;
        }
    }
    for (size_t i = 0; i < a.time.size(); ++i)
    {
        bool gap = !std::isfinite(a.time[i]);
        for (const auto& v : a.values)
            gap = gap || !std::isfinite(v[i]);
        if (gap)
            ++a.gaps;
    }
    if (a.gaps > 0)
        a.warnings.add(juce::String(a.gaps) + " samples have no finite value; the trace breaks there.");
    a.ok = true;
    return a;
}

Acquisition acquire(const analytics::Result& result, const Plan& plan)
{
    Acquisition a;
    if (!result.ok)
    {
        a.error = result.error.isNotEmpty() ? result.error : juce::String("The transient run failed.");
        return a;
    }
    static const std::vector<double> zeros;
    std::vector<SignalSeries> series;
    std::vector<std::vector<double>> groundValues; // a + side on ground reads 0 on the other side's time base
    groundValues.reserve(plan.signals.size());
    for (const auto& s : plan.signals)
    {
        if (s.problem.isNotEmpty())
            continue;
        SignalSeries ss;
        ss.label = s.label;
        ss.unit = s.unit;
        const auto* plus = s.plusTrace.isNotEmpty() ? analytics::findTrace(result, s.plusTrace) : nullptr;
        const auto* minus = s.minusTrace.isNotEmpty() ? analytics::findTrace(result, s.minusTrace) : nullptr;
        if ((s.plusTrace.isNotEmpty() && plus == nullptr) || (s.minusTrace.isNotEmpty() && minus == nullptr))
        {
            a.error = "The run has no trace " + (plus == nullptr && s.plusTrace.isNotEmpty() ? s.plusTrace : s.minusTrace) + " for " + s.label + ".";
            return a;
        }
        if (plus == nullptr) // ground minus a net: 0 on the net's time base
        {
            groundValues.emplace_back(minus->x.size(), 0.0);
            ss.plus = { &minus->x, &groundValues.back() };
        }
        else
            ss.plus = { &plus->x, &plus->y };
        if (minus != nullptr)
            ss.minus = { &minus->x, &minus->y };
        series.push_back(ss);
    }
    a = synchronise(series);
    for (const auto& problem : plan.problems)
        a.warnings.add(problem);
    return a;
}

std::vector<int> displayIndices(const Acquisition& acq, const std::vector<int>& keep, int maxPoints)
{
    const auto n = (int)acq.size();
    std::vector<int> out;
    if (n == 0)
        return out;
    if (maxPoints <= 0 || n <= maxPoints)
    {
        out.resize((size_t)n);
        for (int i = 0; i < n; ++i)
            out[(size_t)i] = i;
        return out;
    }
    const auto perBucket = 2 + 2 * (int)keep.size();
    const auto buckets = std::max(1, maxPoints / perBucket);
    const auto bucketSize = (n + buckets - 1) / buckets;
    std::vector<int> picks;
    for (int start = 0; start < n; start += bucketSize)
    {
        const auto end = std::min(n, start + bucketSize);
        picks.clear();
        picks.push_back(start);
        for (int i = start; i < end; ++i)
        {
            bool gap = false;
            for (auto k : keep)
                gap = gap || !std::isfinite(acq.values[(size_t)k][(size_t)i]);
            if (gap) { picks.push_back(i); break; }
        }
        for (auto k : keep)
        {
            const auto& v = acq.values[(size_t)k];
            int lo = -1, hi = -1;
            for (int i = start; i < end; ++i)
            {
                if (!std::isfinite(v[(size_t)i])) continue;
                if (lo < 0 || v[(size_t)i] < v[(size_t)lo]) lo = i;
                if (hi < 0 || v[(size_t)i] > v[(size_t)hi]) hi = i;
            }
            if (lo >= 0) picks.push_back(lo);
            if (hi >= 0) picks.push_back(hi);
        }
        std::sort(picks.begin(), picks.end());
        picks.erase(std::unique(picks.begin(), picks.end()), picks.end());
        out.insert(out.end(), picks.begin(), picks.end());
    }
    if (out.back() != n - 1)
        out.push_back(n - 1);
    return out;
}

Range autoRange(const std::vector<double>& values)
{
    Range r;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (auto v : values)
        if (std::isfinite(v))
        {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    if (!(lo <= hi))
        return r;
    const auto span = hi - lo;
    if (span <= 1e-12 * std::max(1.0, std::abs(hi)))
    {
        const auto pad = lo == 0.0 ? 1.0 : std::abs(lo) * 0.1;
        r = { lo - pad, hi + pad, true };
        return r;
    }
    r = { lo - span * 0.05, hi + span * 0.05, true };
    return r;
}

Range applyManual(Range r, const juce::String& minText, const juce::String& maxText)
{
    auto manual = r;
    bool ok = false;
    if (minText.trim().isNotEmpty() && !minText.trim().equalsIgnoreCase("auto"))
    {
        const auto v = parseNumber(minText, 0.0, &ok);
        if (ok) { manual.min = v; manual.valid = true; }
    }
    if (maxText.trim().isNotEmpty() && !maxText.trim().equalsIgnoreCase("auto"))
    {
        const auto v = parseNumber(maxText, 0.0, &ok);
        if (ok) { manual.max = v; manual.valid = true; }
    }
    return manual.valid && manual.min < manual.max ? manual : r;
}

juce::String toString(const Camera& c)
{
    return "yaw=" + juce::String(c.yaw, 1) + " pitch=" + juce::String(c.pitch, 1) + " zoom=" + juce::String(c.zoom, 3)
         + " panx=" + juce::String(c.panX, 3) + " pany=" + juce::String(c.panY, 3);
}

Camera parseCamera(const juce::String& text)
{
    Camera c;
    for (const auto& token : juce::StringArray::fromTokens(text, " ;,", ""))
    {
        const auto key = token.upToFirstOccurrenceOf("=", false, false).trim().toLowerCase();
        const auto value = token.fromFirstOccurrenceOf("=", false, false).trim();
        if (value.isEmpty() || !value.containsOnly("0123456789.-+eE"))
            continue;
        const auto v = value.getDoubleValue();
        if (!std::isfinite(v)) continue;
        if (key == "yaw") c.yaw = v;
        else if (key == "pitch") c.pitch = juce::jlimit(-89.0, 89.0, v);
        else if (key == "zoom") c.zoom = juce::jlimit(0.05, 50.0, v);
        else if (key == "panx") c.panX = v;
        else if (key == "pany") c.panY = v;
    }
    return c;
}

Projected project(const Camera& c, double x, double y, double z, juce::Rectangle<float> area)
{
    const auto yaw = juce::degreesToRadians(c.yaw), pitch = juce::degreesToRadians(c.pitch);
    const auto x1 = x * std::cos(yaw) - y * std::sin(yaw);
    const auto y1 = x * std::sin(yaw) + y * std::cos(yaw);
    const auto up = z * std::cos(pitch) + y1 * std::sin(pitch);
    const auto depth = y1 * std::cos(pitch) - z * std::sin(pitch);
    const auto m = (double)std::min(area.getWidth(), area.getHeight());
    const auto s = c.perspective ? 4.0 / (4.0 + depth) : 1.0;
    const auto scale = 0.32 * c.zoom * m;
    Projected p;
    p.x = (float)(area.getCentreX() + x1 * s * scale + c.panX * m);
    p.y = (float)(area.getCentreY() - up * s * scale - c.panY * m);
    p.depth = depth;
    return p;
}

std::vector<double> niceTicks(double min, double max, int count)
{
    std::vector<double> ticks;
    if (!(max > min) || count < 1 || !std::isfinite(min) || !std::isfinite(max))
        return ticks;
    const auto raw = (max - min) / count;
    const auto mag = std::pow(10.0, std::floor(std::log10(raw)));
    const auto f = raw / mag;
    const auto step = (f < 1.5 ? 1.0 : f < 3.5 ? 2.0 : f < 7.5 ? 5.0 : 10.0) * mag;
    for (auto t = std::ceil(min / step) * step; t <= max + step * 1e-9; t += step)
        ticks.push_back(std::abs(t) < step * 1e-9 ? 0.0 : t);
    return ticks;
}

double parseNumber(const juce::String& text, double fallback, bool* ok)
{
    auto t = text.trim().toLowerCase();
    auto fail = [&] { if (ok != nullptr) *ok = false; return fallback; };
    if (t.isEmpty())
        return fail();
    int end = 0;
    while (end < t.length() && juce::String("0123456789.+-e").containsChar(t[end]))
    {
        // an 'e' only belongs to the number when an exponent follows it
        if (t[end] == 'e' && !(end + 1 < t.length() && juce::String("0123456789+-").containsChar(t[end + 1])))
            break;
        ++end;
    }
    const auto number = t.substring(0, end);
    if (number.isEmpty() || !number.containsAnyOf("0123456789"))
        return fail();
    auto v = number.getDoubleValue();
    const auto rest = t.substring(end);
    if (rest.startsWith("meg")) v *= 1e6;
    else if (rest.startsWith("f")) v *= 1e-15;
    else if (rest.startsWith("p")) v *= 1e-12;
    else if (rest.startsWith("n")) v *= 1e-9;
    else if (rest.startsWith("u") || rest.startsWith(juce::CharPointer_UTF8("\xc2\xb5"))) v *= 1e-6;
    else if (rest.startsWith("m")) v *= 1e-3;
    else if (rest.startsWith("k")) v *= 1e3;
    else if (rest.startsWith("g")) v *= 1e9;
    else if (rest.startsWith("t")) v *= 1e12;
    if (ok != nullptr) *ok = std::isfinite(v);
    return std::isfinite(v) ? v : fallback;
}
}

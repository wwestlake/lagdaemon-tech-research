#include "Analytics.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>

namespace analytics
{
namespace
{
using circuit_sim::Element;
using ElementType = Element::Type;
using Complex = std::complex<double>;
constexpr double pi = 3.14159265358979323846;

const juce::String ohm = juce::String(juce::CharPointer_UTF8("\xce\xa9"));
const juce::String degree = juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
const juce::String rootHz = juce::String(juce::CharPointer_UTF8("/\xe2\x88\x9aHz"));

// ---- settings ------------------------------------------------------------------

juce::String text(const Settings& s, const juce::String& key, const juce::String& fallback = {})
{
    const auto found = s.find(key);
    return found != s.end() && found->second.trim().isNotEmpty() ? found->second.trim() : fallback;
}

bool isAuto(const Settings& s, const juce::String& key)
{
    return text(s, key).equalsIgnoreCase("auto");
}

double number(const Settings& s, const juce::String& key, double fallback)
{
    const auto t = text(s, key);
    if (t.isEmpty() || t.equalsIgnoreCase("auto"))
        return fallback;
    double v = 0.0;
    return circuit_sim::parseValue(t.toStdString(), v) ? v : fallback;
}

int integer(const Settings& s, const juce::String& key, int fallback)
{
    const auto t = text(s, key);
    return t.isEmpty() ? fallback : juce::jmax(1, (int)std::lround(number(s, key, fallback)));
}

// ---- names ---------------------------------------------------------------------

juce::String typeName(ElementType t)
{
    switch (t)
    {
        case ElementType::Resistor: return "Resistor";
        case ElementType::Capacitor: return "Capacitor";
        case ElementType::Inductor: return "Inductor";
        case ElementType::Coupling: return "Coupling";
        case ElementType::VoltageSource: return "Voltage source";
        case ElementType::CurrentSource: return "Current source";
        case ElementType::Vcvs: return "VCVS";
        case ElementType::Vccs: return "VCCS";
        case ElementType::Ccvs: return "CCVS";
        case ElementType::Cccs: return "CCCS";
        case ElementType::Diode: return "Diode";
        case ElementType::Npn: return "NPN";
        case ElementType::Pnp: return "PNP";
        case ElementType::Nmos: return "NMOS";
        case ElementType::Pmos: return "PMOS";
        case ElementType::OpAmp: return "Op amp";
    }
    return {};
}

juce::StringArray terminalNames(ElementType t)
{
    switch (t)
    {
        case ElementType::VoltageSource: case ElementType::CurrentSource:
        case ElementType::Ccvs: case ElementType::Cccs: return { "+", "-" };
        case ElementType::Vcvs: case ElementType::Vccs: return { "+", "-", "C+", "C-" };
        case ElementType::Diode: return { "A", "K" };
        case ElementType::Npn: case ElementType::Pnp: return { "C", "B", "E" };
        case ElementType::Nmos: case ElementType::Pmos: return { "D", "G", "S" };
        case ElementType::OpAmp: return { "IN+", "IN-", "OUT", "V+", "V-" };
        default: return { "1", "2" };
    }
}

juce::String unitFor(ElementType t, const juce::String& parameter)
{
    const bool current = t == ElementType::CurrentSource;
    if (parameter == "value")
    {
        switch (t)
        {
            case ElementType::Resistor: return ohm;
            case ElementType::Capacitor: return "F";
            case ElementType::Inductor: return "H";
            case ElementType::Vcvs: case ElementType::Cccs: return "";
            case ElementType::Vccs: return "S";
            case ElementType::Ccvs: return ohm;
            default: return "";
        }
    }
    if (parameter == "dc" || parameter == "amplitude" || parameter == "ac") return current ? "A" : "V";
    if (parameter == "frequency") return "Hz";
    if (parameter == "is") return "A";
    if (parameter == "bv" || parameter == "vaf" || parameter == "vth") return "V";
    if (parameter == "tt" || parameter == "tf") return "s";
    if (parameter == "k") return "A/V^2";
    if (parameter == "lambda") return "1/V";
    if (parameter == "tc1") return "1/K";
    if (parameter == "tc2") return "1/K^2";
    return "";
}

// Elements the user did not place directly (device capacitances, instrument loads).
bool isInternal(const Element& e) { return e.name.find('.') != std::string::npos; }

// ---- lookups -----------------------------------------------------------------------

int nodeOf(const Netlist& n, juce::String name, juce::String& error)
{
    name = name.trim();
    if (name.startsWithIgnoreCase("V(") && name.endsWith(")"))
        name = name.substring(2, name.length() - 1).trim();
    if (name.isEmpty() || name.equalsIgnoreCase("GND") || name == "0")
        return 0;
    for (const auto& net : n.nets)
        if (net.name.equalsIgnoreCase(name))
            return net.node;
    error = "No net named " + name + ". Nets: " + netChoices(n).joinIntoString(", ");
    return -1;
}

int elementOf(const Netlist& n, const juce::String& name)
{
    for (const auto& p : n.parts)
        if (p.refdes.equalsIgnoreCase(name.trim()))
            return p.element;
    return n.circuit.find(name.trim().toStdString());
}

int sourceOf(const Netlist& n, const juce::String& name, juce::String& error)
{
    const auto e = elementOf(n, name);
    if (e >= 0)
    {
        const auto t = n.circuit.elements()[(size_t)e].type;
        if (t == ElementType::VoltageSource || t == ElementType::CurrentSource)
            return e;
    }
    error = name.isEmpty() ? juce::String("Choose an input source.") : name + " is not an independent voltage or current source.";
    if (!sourceChoices(n).isEmpty())
        error << " Sources: " << sourceChoices(n).joinIntoString(", ");
    return -1;
}

struct Target
{
    bool none = true, temperature = false;
    int element = -1;
    std::string parameter;
    juce::String label, unit;
};

bool parseTarget(const Netlist& n, const juce::String& raw, Target& t, juce::String& error)
{
    const auto s = raw.trim();
    t = {};
    if (s.isEmpty() || s.equalsIgnoreCase("None"))
        return true;
    t.none = false;
    if (s.equalsIgnoreCase("TEMP") || s.equalsIgnoreCase("temperature"))
    {
        t.temperature = true;
        t.label = "Temperature";
        t.unit = degree + "C";
        return true;
    }
    const auto part = s.upToLastOccurrenceOf(".", false, false);
    const auto parameter = s.fromLastOccurrenceOf(".", false, false).toLowerCase();
    t.element = elementOf(n, part);
    double unused = 0.0;
    if (t.element < 0 || !circuit_sim::getParameter(n.circuit.elements()[(size_t)t.element], parameter.toStdString(), unused))
    {
        error = "Unknown sweep target " + s + ". Use part.parameter (R1.value, V1.dc, Q1.beta) or TEMP.";
        return false;
    }
    t.parameter = parameter.toStdString();
    t.label = s;
    t.unit = unitFor(n.circuit.elements()[(size_t)t.element].type, parameter);
    return true;
}

std::vector<double> sweepValues(const Settings& s, const juce::String& prefix, juce::String& error)
{
    std::vector<double> values;
    const auto spacing = text(s, prefix + "_spacing", "Linear");
    if (spacing == "List")
    {
        for (auto token : juce::StringArray::fromTokens(text(s, prefix + "_list"), ", ;", ""))
        {
            double v = 0.0;
            if (token.trim().isNotEmpty() && circuit_sim::parseValue(token.trim().toStdString(), v))
                values.push_back(v);
        }
        if (values.empty())
            error = "The value list is empty.";
        return values;
    }
    const auto start = number(s, prefix + "_start", 0.0), stop = number(s, prefix + "_stop", 1.0);
    const auto count = juce::jlimit(1, 100000, integer(s, prefix + "_count", 11));
    if (spacing == "Log")
    {
        if (start <= 0.0 || stop <= 0.0)
        {
            error = "A logarithmic sweep needs positive start and stop values.";
            return values;
        }
        for (int i = 0; i < count; ++i)
            values.push_back(count == 1 ? start : start * std::pow(stop / start, (double)i / (count - 1)));
        return values;
    }
    for (int i = 0; i < count; ++i)
        values.push_back(count == 1 ? start : start + (stop - start) * i / (count - 1));
    return values;
}

struct Variant
{
    juce::String label;
    circuit_sim::Circuit circuit;
    double temperature = circuit_sim::nominalTemperatureC;
};

std::vector<Variant> variants(const Netlist& n, const Settings& s, juce::String& error)
{
    const auto temperature = number(s, "temperature", circuit_sim::nominalTemperatureC);
    Target target;
    if (!parseTarget(n, text(s, "step_target", "None"), target, error))
        return {};
    if (target.none)
        return { { {}, circuit_sim::atTemperature(n.circuit, temperature), temperature } };
    const auto values = sweepValues(s, "step", error);
    if (values.empty())
        return {};
    if (values.size() > 50)
    {
        error = "A parametric step is limited to 50 values.";
        return {};
    }
    std::vector<Variant> out;
    for (auto v : values)
    {
        if (target.temperature)
            out.push_back({ "T = " + formatNumber(v, degree + "C", 4), circuit_sim::atTemperature(n.circuit, v), v });
        else
        {
            auto c = circuit_sim::atTemperature(n.circuit, temperature);
            circuit_sim::setParameter(c.elements()[(size_t)target.element], target.parameter, v);
            out.push_back({ target.label + " = " + formatNumber(v, target.unit, 4), c, temperature });
        }
    }
    return out;
}

juce::String suffix(const Variant& v) { return v.label.isEmpty() ? juce::String() : "  [" + v.label + "]"; }

// ---- outputs -------------------------------------------------------------------------

struct Output
{
    juce::String label;   // V(out), I(R1)
    bool current = false;
    int node = 0;
    int element = -1;
};

std::vector<Output> outputsOf(const Netlist& n, const juce::String& spec, juce::String& error, juce::StringArray& warnings)
{
    std::vector<Output> outs;
    auto tokens = juce::StringArray::fromTokens(spec, ",;", "");
    tokens.trim();
    tokens.removeEmptyStrings();
    if (tokens.size() == 1 && tokens[0].containsChar(' '))
    {
        tokens = juce::StringArray::fromTokens(tokens[0], " ", "");
        tokens.removeEmptyStrings();
    }
    if (tokens.isEmpty())
    {
        for (const auto& net : n.nets)
        {
            if (outs.size() >= 16) { warnings.add("Showing the first 16 nets; list outputs to choose others."); break; }
            outs.push_back({ "V(" + net.name + ")", false, net.node, -1 });
        }
        return outs;
    }
    for (const auto& token : tokens)
    {
        if (token.startsWithIgnoreCase("I(") && token.endsWith(")"))
        {
            const auto part = token.substring(2, token.length() - 1).trim();
            const auto e = elementOf(n, part);
            if (e < 0) { error = "No part " + part + " for " + token + "."; return {}; }
            outs.push_back({ "I(" + part + ")", true, 0, e });
            continue;
        }
        const auto node = nodeOf(n, token, error);
        if (node < 0) return {};
        auto name = token;
        if (name.startsWithIgnoreCase("V(")) name = name.substring(2, name.length() - 1).trim();
        outs.push_back({ "V(" + name + ")", false, node, -1 });
    }
    return outs;
}

double dcValue(const circuit_sim::Circuit& c, const circuit_sim::OperatingPoint& op, const Output& o)
{
    if (!o.current)
        return op.voltages[(size_t)o.node];
    const auto currents = circuit_sim::terminalCurrents(c, op, o.element);
    return currents.empty() ? 0.0 : currents[0];
}

bool acValue(const circuit_sim::Circuit& c, const circuit_sim::AcResult& ac, size_t point, const Output& o, Complex& out)
{
    const auto& v = ac.voltages[point];
    if (!o.current)
    {
        out = v[(size_t)o.node];
        return true;
    }
    const auto& e = c.elements()[(size_t)o.element];
    const auto w = 2.0 * pi * ac.frequency[point];
    auto across = [&] { return v[(size_t)e.nodes[0]] - v[(size_t)e.nodes[1]]; };
    switch (e.type)
    {
        case ElementType::Resistor: out = across() / std::max(e.value, 1e-9); return true;
        case ElementType::Capacitor: out = Complex(0.0, w * e.value) * across(); return true;
        case ElementType::Inductor: case ElementType::VoltageSource: case ElementType::Vcvs:
        case ElementType::Ccvs: case ElementType::OpAmp:
            out = ac.branchCurrents[point][(size_t)o.element];
            return true;
        case ElementType::Vccs: out = e.value * (v[(size_t)e.nodes[2]] - v[(size_t)e.nodes[3]]); return true;
        case ElementType::CurrentSource: out = std::polar(e.wave.acMagnitude, e.wave.acPhaseDegrees * pi / 180.0); return true;
        default: return false;
    }
}

std::vector<double> transientValues(const circuit_sim::Circuit& c, const circuit_sim::TransientResult& tr, const Output& o)
{
    std::vector<double> y(tr.time.size());
    if (!o.current)
    {
        for (size_t s = 0; s < y.size(); ++s) y[s] = tr.voltages[s][(size_t)o.node];
        return y;
    }
    const auto& e = c.elements()[(size_t)o.element];
    if (e.type == ElementType::Capacitor)
    {
        for (size_t s = 0; s < y.size(); ++s)
        {
            const auto a = s > 0 ? s - 1 : s, b = s + 1 < y.size() ? s + 1 : s;
            const auto dt = tr.time[b] - tr.time[a];
            auto vAt = [&](size_t k) { return tr.voltages[k][(size_t)e.nodes[0]] - tr.voltages[k][(size_t)e.nodes[1]]; };
            y[s] = dt > 0.0 ? e.value * (vAt(b) - vAt(a)) / dt : 0.0;
        }
        return y;
    }
    for (size_t s = 0; s < y.size(); ++s)
    {
        if (e.type == ElementType::CurrentSource) { y[s] = e.wave.valueAt(tr.time[s]); continue; }
        circuit_sim::OperatingPoint op;
        op.ok = true;
        op.voltages = tr.voltages[s];
        op.sourceCurrents = tr.sourceCurrents[s];
        const auto currents = circuit_sim::terminalCurrents(c, op, o.element);
        y[s] = currents.empty() ? 0.0 : currents[0];
    }
    return y;
}

double lowestSourceFrequency(const circuit_sim::Circuit& c)
{
    double lowest = 0.0;
    for (const auto& e : c.elements())
    {
        if (e.type != ElementType::VoltageSource && e.type != ElementType::CurrentSource)
            continue;
        double f = 0.0;
        if ((e.wave.kind == circuit_sim::Waveform::Kind::Sine || e.wave.kind == circuit_sim::Waveform::Kind::Square) && e.wave.frequency > 0.0)
            f = e.wave.frequency;
        if (e.wave.kind == circuit_sim::Waveform::Kind::Pulse && e.wave.period > 0.0)
            f = 1.0 / e.wave.period;
        if (f > 0.0)
            lowest = lowest == 0.0 ? f : std::min(lowest, f);
    }
    return lowest;
}

std::vector<double> unwrapDegrees(std::vector<double> phase)
{
    for (size_t i = 1; i < phase.size(); ++i)
    {
        while (phase[i] - phase[i - 1] > 180.0) phase[i] -= 360.0;
        while (phase[i] - phase[i - 1] < -180.0) phase[i] += 360.0;
    }
    return phase;
}

// Line plots split by unit (voltages and currents on their own axes).
void addLinePlots(Result& r, const juce::String& title, const juce::String& xLabel, const juce::String& xUnit, bool logX,
                  std::vector<Trace> traces)
{
    std::vector<juce::String> units;
    for (const auto& t : traces)
        if (std::find(units.begin(), units.end(), t.unit) == units.end())
            units.push_back(t.unit);
    for (const auto& unit : units)
    {
        Plot p;
        p.title = title + (units.size() > 1 ? (unit == "A" ? juce::String(" - currents") : unit == "V" ? juce::String(" - voltages") : " (" + unit + ")") : juce::String());
        p.xLabel = xLabel;
        p.xUnit = xUnit;
        p.yLabel = unit == "A" ? "Current" : unit == "V" ? "Voltage" : unit;
        p.yUnit = unit;
        p.logX = logX;
        for (const auto& t : traces)
            if (t.unit == unit)
                p.traces.push_back(t);
        r.plots.push_back(std::move(p));
    }
}

signal_measure::Result quick(signal_measure::Kind kind, const Trace& t, const std::vector<double>& phase = {})
{
    signal_measure::Request req;
    req.kind = kind;
    return signal_measure::measure(req, t.x, t.y, phase);
}

juce::String measured(signal_measure::Kind kind, const Trace& t, const juce::String& unit, const std::vector<double>& phase = {})
{
    const auto m = quick(kind, t, phase);
    return m.ok ? formatNumber(m.value, unit) : juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));
}

void addTraceStatsTableInternal(Result& r, const std::vector<Trace>& traces, bool withFrequency)
{
    Table t;
    t.title = "Trace statistics";
    t.columns = { "Trace", "Min", "Max", "Peak-peak", "Average", "RMS" };
    if (withFrequency) t.columns.add("Frequency");
    for (const auto& tr : traces)
    {
        juce::StringArray row { tr.name, measured(signal_measure::Kind::Minimum, tr, tr.unit), measured(signal_measure::Kind::Maximum, tr, tr.unit),
                                measured(signal_measure::Kind::PeakToPeak, tr, tr.unit), measured(signal_measure::Kind::Average, tr, tr.unit),
                                measured(signal_measure::Kind::Rms, tr, tr.unit) };
        if (withFrequency) row.add(measured(signal_measure::Kind::Frequency, tr, "Hz"));
        t.rows.push_back(row);
    }
    r.tables.push_back(std::move(t));
}

// ---- analyses ---------------------------------------------------------------------------

bool runOperatingPoint(const Netlist& n, const Settings& s, Result& r)
{
    const auto temperature = number(s, "temperature", circuit_sim::nominalTemperatureC);
    const auto c = circuit_sim::atTemperature(n.circuit, temperature);
    const auto op = circuit_sim::solveOperatingPoint(c);
    if (!op.ok) { r.error = "Operating point failed: " + juce::String(op.error); return false; }

    Table nodes;
    nodes.title = "Node voltages";
    nodes.columns = { "Net", "Voltage", "Pins" };
    for (const auto& net : n.nets)
        nodes.rows.push_back({ net.name, formatNumber(op.voltages[(size_t)net.node], "V", 6), net.pins });
    r.tables.push_back(nodes);

    Table currents;
    currents.title = "Element currents and power";
    currents.columns = { "Element", "Type", "Current into each terminal", "Power absorbed" };
    Table sources;
    sources.title = "Sources";
    sources.columns = { "Source", "Voltage across", "Current (out of +)", "Power delivered" };
    double delivered = 0.0, dissipated = 0.0;
    const auto& parts = c.elements();
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto& e = parts[i];
        if (e.type == ElementType::Coupling)
            continue;
        const auto power = circuit_sim::absorbedPower(c, op, (int)i);
        if (e.type == ElementType::VoltageSource || e.type == ElementType::CurrentSource)
        {
            const auto v = op.voltages[(size_t)e.nodes[0]] - op.voltages[(size_t)e.nodes[1]];
            const auto out = -circuit_sim::terminalCurrents(c, op, (int)i)[0];
            sources.rows.push_back({ e.name, formatNumber(v, "V", 5), formatNumber(out, "A", 5), formatNumber(-power, "W", 4) });
            delivered += -power;
            continue;
        }
        if (power > 0.0) dissipated += power;
        if (isInternal(e))
            continue;
        const auto names = terminalNames(e.type);
        const auto ti = circuit_sim::terminalCurrents(c, op, (int)i);
        juce::StringArray cells;
        for (size_t t = 0; t < ti.size(); ++t)
            if (e.type != ElementType::OpAmp || t == 2)
                cells.add(names[(int)t] + ": " + formatNumber(ti[t], "A", 4));
        currents.rows.push_back({ e.name, typeName(e.type), cells.joinIntoString("   "), formatNumber(power, "W", 4) });
    }
    sources.rows.push_back({ "Total", "", "", formatNumber(delivered, "W", 4) });
    r.tables.push_back(sources);
    r.tables.push_back(currents);

    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto info = circuit_sim::deviceInfo(c, op, (int)i);
        if (info.values.empty())
            continue;
        Table t;
        t.title = juce::String(parts[i].name) + "  (" + typeName(parts[i].type) + ")  -  " + juce::String(info.region);
        t.columns = { "Quantity", "Value" };
        for (size_t k = 0; k < info.values.size(); ++k)
        {
            auto unit = juce::String(info.units[k]);
            if (unit == "ohm") unit = ohm;
            t.rows.push_back({ info.values[k].first, formatNumber(info.values[k].second, unit, 5) });
        }
        r.tables.push_back(t);
    }
    r.summary = "Converged in " + juce::String(op.iterations) + " Newton iteration(s) at " + formatNumber(temperature, degree + "C", 4)
              + ". Sources deliver " + formatNumber(delivered, "W", 4) + "; " + juce::String((int)n.nets.size()) + " nets.";
    return true;
}

bool runDcSweep(const Netlist& n, const Settings& s, Result& r)
{
    Target inner, outer;
    if (!parseTarget(n, text(s, "sweep_target"), inner, r.error)) return false;
    if (inner.none) { r.error = "Choose what to sweep (a source, a part value, or TEMP)."; return false; }
    if (!parseTarget(n, text(s, "step_target", "None"), outer, r.error)) return false;
    const auto innerValues = sweepValues(s, "sweep", r.error);
    if (innerValues.empty()) return false;
    std::vector<double> outerValues;
    if (!outer.none)
    {
        outerValues = sweepValues(s, "step", r.error);
        if (outerValues.empty()) return false;
    }
    const auto outs = outputsOf(n, text(s, "outputs"), r.error, r.warnings);
    if (outs.empty()) { if (r.error.isEmpty()) r.error = "No outputs to plot."; return false; }

    const bool anyTemperature = inner.temperature || outer.temperature;
    const auto base = anyTemperature ? n.circuit : circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    circuit_sim::SweepAxis a, b;
    a.element = inner.element; a.parameter = inner.parameter; a.temperature = inner.temperature; a.values = innerValues;
    b.element = outer.element; b.parameter = outer.parameter; b.temperature = outer.temperature; b.values = outerValues;
    const auto sweep = circuit_sim::solveDcSweep(base, a, b);
    if (!sweep.ok) { r.error = sweep.error; return false; }

    std::vector<Trace> traces;
    for (size_t o = 0; o < sweep.outer.size(); ++o)
        for (const auto& out : outs)
        {
            Trace t;
            t.name = out.label + (outer.none ? juce::String() : "  [" + outer.label + " = " + formatNumber(sweep.outer[o], outer.unit, 4) + "]");
            t.unit = out.current ? "A" : "V";
            t.x = sweep.inner;
            for (size_t k = 0; k < sweep.inner.size(); ++k)
            {
                auto c = base;
                if (!inner.temperature)
                    circuit_sim::setParameter(c.elements()[(size_t)inner.element], inner.parameter, sweep.inner[k]);
                t.y.push_back(dcValue(c, sweep.points[o][k], out));
            }
            traces.push_back(std::move(t));
        }
    addLinePlots(r, "DC sweep", inner.label, inner.unit, text(s, "sweep_spacing") == "Log", traces);
    Table ends;
    ends.title = "Sweep end points";
    ends.columns = { "Trace", "At " + formatNumber(innerValues.front(), inner.unit), "At " + formatNumber(innerValues.back(), inner.unit), "Min", "Max" };
    for (const auto& t : traces)
        ends.rows.push_back({ t.name, formatNumber(t.y.front(), t.unit), formatNumber(t.y.back(), t.unit),
                              measured(signal_measure::Kind::Minimum, t, t.unit), measured(signal_measure::Kind::Maximum, t, t.unit) });
    r.tables.push_back(ends);
    r.summary = "Swept " + inner.label + " over " + juce::String((int)innerValues.size()) + " points"
              + (outer.none ? juce::String() : " for " + juce::String((int)outerValues.size()) + " values of " + outer.label) + ".";
    return true;
}

std::vector<double> acFrequencies(const Settings& s, juce::String& error)
{
    const auto start = number(s, "start", 10.0), stop = number(s, "stop", 1e6);
    std::vector<double> f;
    if (start <= 0.0 || stop <= start) { error = "The frequency sweep needs 0 < start < stop."; return f; }
    const auto points = juce::jlimit(1, 20000, integer(s, "points", 50));
    if (text(s, "spacing", "Decade") == "Linear")
    {
        for (int i = 0; i < std::max(2, points); ++i)
            f.push_back(start + (stop - start) * i / (std::max(2, points) - 1));
        return f;
    }
    const auto decades = std::log10(stop / start);
    const auto count = std::max(2, (int)std::ceil(decades * points) + 1);
    for (int i = 0; i < count; ++i)
        f.push_back(start * std::pow(10.0, decades * i / (count - 1)));
    return f;
}

circuit_sim::Circuit drivenBy(circuit_sim::Circuit c, int input)
{
    for (size_t i = 0; i < c.elements().size(); ++i)
    {
        c.elements()[i].wave.acMagnitude = (int)i == input ? 1.0 : 0.0;
        c.elements()[i].wave.acPhaseDegrees = 0.0;
    }
    return c;
}

bool runAc(const Netlist& n, const Settings& s, Result& r)
{
    const auto input = sourceOf(n, text(s, "input"), r.error);
    if (input < 0) return false;
    const auto freqs = acFrequencies(s, r.error);
    if (freqs.empty()) return false;
    const auto outs = outputsOf(n, text(s, "outputs"), r.error, r.warnings);
    if (outs.empty()) { if (r.error.isEmpty()) r.error = "No outputs to plot."; return false; }
    const auto vars = variants(n, s, r.error);
    if (vars.empty()) return false;
    const bool decibels = text(s, "magnitude", "dB") == "dB";
    const bool logX = text(s, "spacing", "Decade") != "Linear";
    const auto inputName = juce::String(n.circuit.elements()[(size_t)input].name);

    Plot mag, phase, delay, imag, iphase;
    mag.title = "Magnitude (relative to 1 " + juce::String(n.circuit.elements()[(size_t)input].type == ElementType::VoltageSource ? "V" : "A") + " at " + inputName + ")";
    mag.yLabel = decibels ? "Magnitude" : "Magnitude"; mag.yUnit = decibels ? "dB" : "V";
    phase.title = "Phase"; phase.yLabel = "Phase"; phase.yUnit = degree;
    delay.title = "Group delay"; delay.yLabel = "Group delay"; delay.yUnit = "s";
    imag.title = "Current magnitude"; imag.yLabel = "Current"; imag.yUnit = "A"; imag.logY = true;
    iphase.title = "Current phase"; iphase.yLabel = "Phase"; iphase.yUnit = degree;
    for (auto* p : { &mag, &phase, &delay, &imag, &iphase }) { p->xLabel = "Frequency"; p->xUnit = "Hz"; p->logX = logX; }

    Table summary;
    summary.title = "AC summary";
    summary.columns = { "Output", "Peak", "At", "Lower -3 dB", "Upper -3 dB", "0 dB crossing", "PM (if a loop gain)" };
    for (const auto& v : vars)
    {
        const auto c = drivenBy(v.circuit, input);
        const auto ac = circuit_sim::solveAcAt(c, freqs);
        if (!ac.ok) { r.error = "AC analysis failed" + suffix(v) + ": " + juce::String(ac.error); return false; }
        for (const auto& out : outs)
        {
            std::vector<Complex> h;
            bool available = true;
            for (size_t k = 0; k < ac.frequency.size() && available; ++k)
            {
                Complex value;
                available = acValue(c, ac, k, out, value);
                h.push_back(value);
            }
            if (!available) { r.warnings.add(out.label + " has no small-signal current model; left out."); continue; }
            Trace m, p, d;
            m.name = p.name = d.name = out.label + suffix(v);
            m.x = p.x = d.x = ac.frequency;
            std::vector<double> rawPhase;
            for (const auto& value : h)
            {
                const auto magnitude = std::abs(value);
                m.y.push_back(out.current ? magnitude : decibels ? 20.0 * std::log10(std::max(1e-30, magnitude)) : magnitude);
                rawPhase.push_back(std::arg(value) * 180.0 / pi);
            }
            p.y = unwrapDegrees(rawPhase);
            for (size_t k = 0; k < p.y.size(); ++k)
            {
                const auto a = k > 0 ? k - 1 : k, b = k + 1 < p.y.size() ? k + 1 : k;
                const auto dw = 2.0 * pi * (ac.frequency[b] - ac.frequency[a]);
                d.y.push_back(dw > 0.0 ? -(p.y[b] - p.y[a]) * pi / 180.0 / dw : 0.0);
            }
            auto& magPlot = out.current ? imag : mag;
            auto& phasePlot = out.current ? iphase : phase;
            m.unit = out.current ? "A" : decibels ? "dB" : "V";
            p.unit = degree;
            d.unit = "s";
            m.partnerPlot = 1; // patched below once plot order is known
            m.partnerTrace = (int)phasePlot.traces.size();
            p.partnerTrace = (int)magPlot.traces.size();
            if (!out.current && decibels)
            {
                summary.rows.push_back({ m.name, measured(signal_measure::Kind::Maximum, m, "dB"), measured(signal_measure::Kind::PeakX, m, "Hz"),
                                         measured(signal_measure::Kind::LowerCorner3dB, m, "Hz"), measured(signal_measure::Kind::UpperCorner3dB, m, "Hz"),
                                         measured(signal_measure::Kind::UnityGainFrequency, m, "Hz"), measured(signal_measure::Kind::PhaseMargin, m, degree, p.y) });
            }
            magPlot.traces.push_back(std::move(m));
            phasePlot.traces.push_back(std::move(p));
            if (!out.current) delay.traces.push_back(std::move(d));
        }
    }
    auto addPair = [&](Plot& a, Plot& b) {
        if (a.traces.empty()) return;
        const auto ai = (int)r.plots.size(), bi = ai + 1;
        for (auto& t : a.traces) t.partnerPlot = bi;
        for (auto& t : b.traces) t.partnerPlot = ai;
        r.plots.push_back(a);
        r.plots.push_back(b);
    };
    addPair(mag, phase);
    if (!delay.traces.empty()) r.plots.push_back(delay);
    addPair(imag, iphase);
    if (!summary.rows.empty()) r.tables.push_back(summary);
    r.summary = "AC sweep " + formatNumber(freqs.front(), "Hz") + " to " + formatNumber(freqs.back(), "Hz") + ", " + juce::String((int)freqs.size())
              + " points, input " + inputName + (vars.size() > 1 ? ", " + juce::String((int)vars.size()) + " step values" : juce::String()) + ".";
    return true;
}

double autoTransientStep(const circuit_sim::Circuit& c, double stop)
{
    auto step = stop / 2000.0;
    const auto f = lowestSourceFrequency(c);
    double highest = 0.0;
    for (const auto& e : c.elements())
        if ((e.type == ElementType::VoltageSource || e.type == ElementType::CurrentSource) && e.wave.frequency > highest
            && (e.wave.kind == circuit_sim::Waveform::Kind::Sine || e.wave.kind == circuit_sim::Waveform::Kind::Square))
            highest = e.wave.frequency;
    if (highest > 0.0) step = std::min(step, 1.0 / (highest * 100.0));
    juce::ignoreUnused(f);
    // Pulse and PWL corners are hit exactly, so edges need no tiny step; resolve the flat parts.
    for (const auto& e : c.elements())
        if (e.wave.kind == circuit_sim::Waveform::Kind::Pulse)
        {
            if (e.wave.period > 0.0) step = std::min(step, e.wave.period / 200.0);
            if (e.wave.width > 0.0) step = std::min(step, e.wave.width / 20.0);
        }
    return std::max(step, stop / 1e6);
}

double autoTransientStop(const circuit_sim::Circuit& c)
{
    const auto f = lowestSourceFrequency(c);
    if (f > 0.0) return 10.0 / f;
    double latest = 0.0;
    for (const auto& e : c.elements())
    {
        if (e.wave.kind == circuit_sim::Waveform::Kind::Pulse) latest = std::max(latest, 2.0 * (e.wave.delay + e.wave.rise + e.wave.width + e.wave.fall));
        if (e.wave.kind == circuit_sim::Waveform::Kind::Pwl && !e.wave.points.empty()) latest = std::max(latest, 1.2 * e.wave.points.back().first);
        if (e.wave.kind == circuit_sim::Waveform::Kind::Exp) latest = std::max(latest, e.wave.delay2 + 5.0 * std::max(e.wave.tau1, e.wave.tau2));
    }
    return latest > 0.0 ? latest : 10e-3;
}

bool runTransient(const Netlist& n, const Settings& s, Result& r)
{
    const auto outs = outputsOf(n, text(s, "outputs"), r.error, r.warnings);
    if (outs.empty()) { if (r.error.isEmpty()) r.error = "No outputs to plot."; return false; }
    const auto vars = variants(n, s, r.error);
    if (vars.empty()) return false;
    circuit_sim::TransientSettings ts;
    ts.stop = isAuto(s, "stop") || text(s, "stop").isEmpty() ? autoTransientStop(n.circuit) : number(s, "stop", 10e-3);
    ts.step = isAuto(s, "step") || text(s, "step").isEmpty() ? autoTransientStep(n.circuit, ts.stop) : number(s, "step", ts.stop / 2000.0);
    ts.start = number(s, "start", 0.0);
    ts.maxSamples = juce::jlimit(100, 200000, integer(s, "max_points", 4000));
    if (ts.stop / ts.step > 5e6) { r.error = "That is more than 5 million time steps; use a larger step or a shorter stop time."; return false; }
    std::vector<Trace> traces;
    for (const auto& v : vars)
    {
        const auto tr = circuit_sim::solveTransient(v.circuit, ts);
        if (!tr.ok) { r.error = "Transient failed" + suffix(v) + ": " + juce::String(tr.error); return false; }
        for (const auto& out : outs)
        {
            Trace t;
            t.name = out.label + suffix(v);
            t.unit = out.current ? "A" : "V";
            t.x = tr.time;
            t.y = transientValues(v.circuit, tr, out);
            traces.push_back(std::move(t));
        }
    }
    addLinePlots(r, "Transient", "Time", "s", false, traces);
    addTraceStatsTableInternal(r, traces, true);
    r.summary = "Transient to " + formatNumber(ts.stop, "s") + " in steps of " + formatNumber(ts.step, "s")
              + " (trapezoidal, source corners hit exactly)" + (ts.start > 0.0 ? ", stored from " + formatNumber(ts.start, "s") : juce::String()) + ".";
    return true;
}

bool runFourier(const Netlist& n, const Settings& s, Result& r)
{
    const auto outName = text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name);
    const auto node = nodeOf(n, outName, r.error);
    if (node < 0) return false;
    const auto c = circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    double f0 = number(s, "fundamental", 0.0);
    juce::String fundamentalFrom;
    if (isAuto(s, "fundamental") || text(s, "fundamental").isEmpty())
    {
        // The periodic source whose signal is largest at this output: amplitude x |H(f)|.
        f0 = 0.0;
        double best = -1.0;
        for (size_t i = 0; i < c.elements().size(); ++i)
        {
            const auto& e = c.elements()[i];
            if (e.type != ElementType::VoltageSource && e.type != ElementType::CurrentSource)
                continue;
            double f = 0.0, amplitude = 0.0;
            if ((e.wave.kind == circuit_sim::Waveform::Kind::Sine || e.wave.kind == circuit_sim::Waveform::Kind::Square) && e.wave.frequency > 0.0)
            {
                f = e.wave.frequency;
                amplitude = std::abs(e.wave.amplitude);
            }
            if (e.wave.kind == circuit_sim::Waveform::Kind::Pulse && e.wave.period > 0.0)
            {
                f = 1.0 / e.wave.period;
                amplitude = std::abs(e.wave.pulsed - e.wave.offset);
            }
            if (f <= 0.0)
                continue;
            const auto ac = circuit_sim::solveAcAt(drivenBy(c, (int)i), { f });
            const auto score = ac.ok ? amplitude * std::abs(ac.voltages[0][(size_t)node]) : 0.0;
            if (score > best)
            {
                best = score;
                f0 = f;
                fundamentalFrom = juce::String(e.name);
            }
        }
    }
    if (f0 <= 0.0) { r.error = "Set the fundamental frequency (no periodic source to take it from)."; return false; }
    const auto harmonics = juce::jlimit(1, 99, integer(s, "harmonics", 9));
    const auto periods = juce::jlimit(1, 100, integer(s, "periods", 1));
    const auto settle = juce::jlimit(0, 10000, (int)std::lround(number(s, "settle_periods", 10.0)));
    circuit_sim::TransientSettings ts;
    ts.stop = (settle + periods) / f0;
    ts.step = 1.0 / (f0 * std::max(200, 40 * harmonics));
    ts.start = settle / f0;
    ts.maxSamples = 1 << 22;
    const auto tr = circuit_sim::solveTransient(c, ts);
    if (!tr.ok) { r.error = "Transient for the Fourier analysis failed: " + juce::String(tr.error); return false; }
    std::vector<double> v;
    for (const auto& sample : tr.voltages) v.push_back(sample[(size_t)node]);
    const auto fr = circuit_sim::fourier(tr.time, v, f0, harmonics, periods);
    if (!fr.ok) { r.error = fr.error; return false; }

    const auto label = "V(" + (node == 0 ? juce::String("GND") : outName) + ")";
    Table t;
    t.title = "Fourier components of " + label;
    t.columns = { "Harmonic", "Frequency", "Magnitude (peak)", "Normalized", "Normalized (dB)", "Phase", "Normalized phase" };
    t.rows.push_back({ "DC", "0 Hz", formatNumber(fr.dc, "V", 5), "", "", "", "" });
    Plot bars;
    bars.kind = Plot::Kind::Bars;
    bars.title = "Harmonics of " + label + " relative to the fundamental";
    bars.xLabel = "Harmonic";
    bars.yLabel = "Level";
    bars.yUnit = "dBc";
    Trace bt;
    bt.name = label;
    bt.unit = "dBc";
    for (int h = 1; h <= harmonics; ++h)
    {
        const auto m = fr.magnitude[(size_t)h - 1];
        const auto norm = fr.magnitude[0] > 0.0 ? m / fr.magnitude[0] : 0.0;
        const auto db = 20.0 * std::log10(std::max(1e-15, norm));
        t.rows.push_back({ juce::String(h), formatNumber(h * f0, "Hz", 5), formatNumber(m, "V", 5), juce::String(norm, 6),
                           juce::String(db, 2) + " dB", juce::String(fr.phaseDegrees[(size_t)h - 1], 2) + degree,
                           juce::String(fr.phaseDegrees[(size_t)h - 1] - fr.phaseDegrees[0], 2) + degree });
        bars.categories.add(h == 1 ? "1 (" + formatNumber(f0, "Hz", 3) + ")" : juce::String(h));
        bt.x.push_back(h - 1);
        bt.y.push_back(db);
    }
    bars.traces.push_back(bt);
    r.tables.push_back(t);
    r.plots.push_back(bars);
    Trace wave;
    wave.name = label;
    wave.unit = "V";
    wave.x = tr.time;
    wave.y = v;
    addLinePlots(r, "Analysed window", "Time", "s", false, { wave });
    r.summary = "THD = " + juce::String(fr.thdPercent, 4) + " % over " + juce::String(harmonics) + " harmonics of " + formatNumber(f0, "Hz")
              + "; fundamental " + formatNumber(fr.magnitude[0], "V", 5) + " peak, DC " + formatNumber(fr.dc, "V", 4)
              + " (last " + juce::String(periods) + " period(s) after " + juce::String(settle) + " settling periods"
              + (fundamentalFrom.isNotEmpty() ? "; fundamental taken from " + fundamentalFrom : juce::String()) + ").";
    return true;
}

bool runNoise(const Netlist& n, const Settings& s, Result& r)
{
    const auto out = nodeOf(n, text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name), r.error);
    if (out < 0) return false;
    const auto ref = nodeOf(n, text(s, "reference", "GND"), r.error);
    if (ref < 0) return false;
    const auto input = sourceOf(n, text(s, "input"), r.error);
    if (input < 0) return false;
    const auto start = number(s, "start", 10.0), stop = number(s, "stop", 100e3);
    const auto ppd = juce::jlimit(1, 1000, integer(s, "points", 20));
    const auto vars = variants(n, s, r.error);
    if (vars.empty()) return false;
    circuit_sim::Options options;
    options.temperatureC = number(s, "temperature", circuit_sim::nominalTemperatureC);
    const bool currentInput = n.circuit.elements()[(size_t)input].type == ElementType::CurrentSource;
    const auto outLabel = "V(" + text(s, "output") + (ref != 0 ? ", " + text(s, "reference") : juce::String()) + ")";

    Plot po, pi_;
    po.title = "Output noise density at " + outLabel;
    po.yLabel = "Noise"; po.yUnit = "V" + rootHz;
    pi_.title = "Input-referred noise density at " + juce::String(n.circuit.elements()[(size_t)input].name);
    pi_.yLabel = "Noise"; pi_.yUnit = (currentInput ? "A" : "V") + rootHz;
    for (auto* p : { &po, &pi_ }) { p->xLabel = "Frequency"; p->xUnit = "Hz"; p->logX = true; p->logY = true; }
    juce::StringArray totals;
    for (size_t vi = 0; vi < vars.size(); ++vi)
    {
        const auto& v = vars[vi];
        options.temperatureC = v.temperature;
        const auto nr = circuit_sim::solveNoise(v.circuit, out, ref, input, start, stop, ppd, options);
        if (!nr.ok) { r.error = "Noise analysis failed" + suffix(v) + ": " + juce::String(nr.error); return false; }
        Trace a, b;
        a.name = "Output" + suffix(v);
        b.name = "Input-referred" + suffix(v);
        a.unit = po.yUnit; b.unit = pi_.yUnit;
        a.x = b.x = nr.frequency;
        a.y = nr.outputDensity;
        b.y = nr.inputDensity;
        po.traces.push_back(a);
        pi_.traces.push_back(b);
        totals.add("output " + formatNumber(nr.integratedOutputRms, "V", 4) + " rms, input-referred "
                   + formatNumber(nr.integratedInputRms, currentInput ? "A" : "V", 4) + " rms" + suffix(v));
        if (vi == 0)
        {
            Table t;
            t.title = "Noise contributions (integrated " + formatNumber(start, "Hz") + " to " + formatNumber(stop, "Hz") + suffix(v) + ")";
            t.columns = { "Element", "Mechanism", "Output noise (rms)", "Share of output power" };
            const auto total = nr.integratedOutputRms * nr.integratedOutputRms;
            for (const auto& cn : nr.contributions)
                t.rows.push_back({ cn.element, cn.source, formatNumber(std::sqrt(cn.integratedOutputV2), "V", 4),
                                   juce::String(total > 0.0 ? 100.0 * cn.integratedOutputV2 / total : 0.0, 2) + " %" });
            r.tables.push_back(t);
            for (const auto& name : nr.noiselessElements)
                if (juce::String(name).containsChar('.') == false)
                    r.warnings.add(juce::String(name) + " is an ideal op amp model and contributes no noise.");
        }
    }
    r.plots.push_back(po);
    r.plots.push_back(pi_);
    r.summary = "Noise " + formatNumber(start, "Hz") + " to " + formatNumber(stop, "Hz") + " at " + formatNumber(options.temperatureC, degree + "C", 4)
              + ": " + totals.joinIntoString("; ") + ".";
    return true;
}

bool runTransferFunction(const Netlist& n, const Settings& s, Result& r)
{
    const auto outName = text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name);
    const auto out = nodeOf(n, outName, r.error);
    if (out < 0) return false;
    const auto ref = nodeOf(n, text(s, "reference", "GND"), r.error);
    if (ref < 0) return false;
    const auto input = sourceOf(n, text(s, "input"), r.error);
    if (input < 0) return false;
    const auto c = circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    const auto tf = circuit_sim::solveTransferFunction(c, out, ref, input);
    if (!tf.ok) { r.error = "Transfer function failed: " + juce::String(tf.error); return false; }
    const auto& source = c.elements()[(size_t)input];
    const bool currentInput = source.type == ElementType::CurrentSource;
    const auto outLabel = "V(" + outName + (ref != 0 ? ", " + text(s, "reference") : juce::String()) + ")";
    Table t;
    t.title = "Small-signal DC transfer function";
    t.columns = { "Quantity", "Value" };
    const auto gainText = currentInput ? formatNumber(tf.gain, ohm, 6)
                        : std::abs(tf.gain) < 1e-12 ? juce::String("0 V/V  (no DC path)")
                        : formatNumber(tf.gain, "V/V", 6) + "  (" + juce::String(20.0 * std::log10(std::abs(tf.gain)), 3) + " dB)";
    t.rows.push_back({ outLabel + " / " + (currentInput ? "I(" : "V(") + juce::String(source.name) + ")", gainText });
    t.rows.push_back({ "Input resistance at " + juce::String(source.name), formatNumber(tf.inputResistance, ohm, 6) });
    t.rows.push_back({ "Output resistance at " + outLabel, formatNumber(tf.outputResistance, ohm, 6) });
    r.tables.push_back(t);
    r.summary = "Gain " + t.rows[0][1] + ", Rin " + t.rows[1][1] + ", Rout " + t.rows[2][1] + ".";
    return true;
}

bool runSensitivity(const Netlist& n, const Settings& s, Result& r)
{
    const auto outName = text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name);
    const auto out = nodeOf(n, outName, r.error);
    if (out < 0) return false;
    const auto ref = nodeOf(n, text(s, "reference", "GND"), r.error);
    if (ref < 0) return false;
    const auto c = circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    const bool ac = text(s, "mode", "DC") == "AC";
    circuit_sim::SensitivityResult sr;
    juce::String outputText;
    if (ac)
    {
        const auto input = sourceOf(n, text(s, "input"), r.error);
        if (input < 0) return false;
        const auto f = number(s, "frequency", 1e3);
        sr = circuit_sim::solveAcSensitivity(c, out, ref, input, f);
        outputText = "gain to V(" + outName + ") at " + formatNumber(f, "Hz");
    }
    else
    {
        sr = circuit_sim::solveDcSensitivity(c, out, ref);
        outputText = "V(" + outName + ")";
    }
    if (!sr.ok) { r.error = "Sensitivity failed: " + juce::String(sr.error); return false; }
    const auto unit = ac ? juce::String("dB") : juce::String("V");
    Table t;
    t.title = "Sensitivity of " + outputText + " (nominal " + (ac ? juce::String(sr.output, 4) + " dB" : formatNumber(sr.output, "V", 6)) + ")";
    t.columns = { "Element", "Parameter", "Value", "d(output)/d(parameter)", "Change per +1 %" };
    Plot bars;
    bars.kind = Plot::Kind::Bars;
    bars.title = "Output change per +1 % change of each parameter (largest 15)";
    bars.xLabel = "Parameter";
    bars.yLabel = "Change";
    bars.yUnit = unit;
    Trace bt;
    bt.name = outputText;
    bt.unit = unit;
    for (const auto& item : sr.items)
    {
        const auto e = n.circuit.find(item.element);
        if (e >= 0 && isInternal(n.circuit.elements()[(size_t)e]))
            continue; // instrument inputs, device capacitances
        const auto punit = e >= 0 ? unitFor(n.circuit.elements()[(size_t)e].type, item.parameter) : juce::String();
        t.rows.push_back({ item.element, item.parameter, formatNumber(item.value, punit, 4),
                           formatNumber(item.absolute, "", 6) + " " + unit + (punit.isNotEmpty() ? "/" + punit : juce::String()),
                           formatNumber(item.normalized, unit, 4) });
        if (bars.categories.size() < 15 && std::abs(item.normalized) > 0.0)
        {
            bars.categories.add(juce::String(item.element) + "." + juce::String(item.parameter));
            bt.x.push_back(bt.x.size());
            bt.y.push_back(item.normalized);
        }
    }
    bars.traces.push_back(bt);
    r.tables.push_back(t);
    r.plots.push_back(bars);
    r.summary = (ac ? "AC" : "DC") + juce::String(" sensitivity of ") + outputText + "; "
              + (sr.items.empty() ? juce::String("no parameters affect it.") : "largest: " + juce::String(sr.items[0].element) + "." + juce::String(sr.items[0].parameter)
                 + " (" + formatNumber(sr.items[0].normalized, unit, 3) + " per +1 %).");
    return true;
}

bool runPoleZero(const Netlist& n, const Settings& s, Result& r)
{
    const auto outName = text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name);
    const auto out = nodeOf(n, outName, r.error);
    if (out < 0) return false;
    const auto ref = nodeOf(n, text(s, "reference", "GND"), r.error);
    if (ref < 0) return false;
    const auto input = sourceOf(n, text(s, "input"), r.error);
    if (input < 0) return false;
    const auto c = circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    const auto pz = circuit_sim::solvePoleZero(c, out, ref, input);
    if (!pz.ok) { r.error = "Pole-zero analysis failed: " + juce::String(pz.error); return false; }
    auto table = [&](const juce::String& title, const std::vector<Complex>& roots) {
        Table t;
        t.title = title;
        t.columns = { "#", "Real (rad/s)", "Imaginary (rad/s)", "|s| / 2 pi", "Damping ratio", "Q" };
        int i = 1;
        double largest = 0.0;
        for (const auto& p : roots) largest = std::max(largest, std::abs(p));
        for (auto p : roots)
        {
            if (std::abs(p) < 1e-9 * std::max(1.0, largest)) p = 0.0; // at the origin, to rounding
            const auto magnitude = std::abs(p);
            const auto zeta = magnitude > 0.0 ? -p.real() / magnitude : 0.0;
            const auto dash = juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));
            t.rows.push_back({ juce::String(i++), formatNumber(p.real(), "", 7), formatNumber(p.imag(), "", 7), formatNumber(magnitude / (2.0 * pi), "Hz", 5),
                               magnitude > 0.0 ? juce::String(zeta, 4) : dash,
                               std::abs(zeta) > 1e-12 && p.imag() != 0.0 ? juce::String(1.0 / (2.0 * zeta), 4) : dash });
        }
        if (roots.empty()) t.rows.push_back({ "", "none (finite)", "", "", "", "" });
        return t;
    };
    r.tables.push_back(table("Poles", pz.poles));
    r.tables.push_back(table("Zeros", pz.zeros));
    Plot p;
    p.kind = Plot::Kind::PoleZero;
    p.title = "s-plane: poles (x) and zeros (o) of V(" + outName + ") / " + juce::String(c.elements()[(size_t)input].name);
    p.xLabel = "Real";
    p.xUnit = "rad/s";
    p.yLabel = "Imaginary";
    p.yUnit = "rad/s";
    p.poles = pz.poles;
    p.zeros = pz.zeros;
    r.plots.push_back(p);
    bool stable = true;
    for (const auto& pole : pz.poles) if (pole.real() > 1e-9 * std::abs(pole)) stable = false;
    r.summary = juce::String((int)pz.poles.size()) + " pole(s), " + juce::String((int)pz.zeros.size()) + " zero(s)"
              + (pz.cancelled > 0 ? " (" + juce::String(pz.cancelled) + " coincident pole-zero pair(s) cancelled: modes this source does not reach)" : juce::String())
              + "; DC gain " + formatNumber(pz.dcGain, "", 6)
              + "; " + (stable ? "all poles in the left half-plane (stable)." : "a pole in the right half-plane: UNSTABLE.")
              + (pz.poles.empty() ? juce::String() : " Lowest pole at " + formatNumber(std::abs(pz.poles.front()) / (2.0 * pi), "Hz") + ".");
    return true;
}

bool runTemperature(const Netlist& n, const Settings& s, Result& r)
{
    const auto outs = outputsOf(n, text(s, "outputs"), r.error, r.warnings);
    if (outs.empty()) { if (r.error.isEmpty()) r.error = "No outputs to plot."; return false; }
    const auto from = number(s, "temp_start", -40.0), to = number(s, "temp_stop", 125.0);
    const auto count = juce::jlimit(2, 2000, integer(s, "temp_points", 34));
    circuit_sim::SweepAxis a, none;
    a.temperature = true;
    for (int i = 0; i < count; ++i) a.values.push_back(from + (to - from) * i / (count - 1));
    const auto sweep = circuit_sim::solveDcSweep(n.circuit, a, none);
    if (!sweep.ok) { r.error = sweep.error; return false; }
    std::vector<Trace> traces;
    Table t;
    t.title = "Operating point versus temperature";
    t.columns.add("Temperature");
    for (const auto& out : outs) t.columns.add(out.label);
    for (const auto& out : outs)
    {
        Trace tr;
        tr.name = out.label;
        tr.unit = out.current ? "A" : "V";
        tr.x = a.values;
        for (size_t k = 0; k < a.values.size(); ++k)
            tr.y.push_back(dcValue(circuit_sim::atTemperature(n.circuit, a.values[k]), sweep.points[0][k], out));
        traces.push_back(std::move(tr));
    }
    const auto stride = std::max<size_t>(1, a.values.size() / 40);
    for (size_t k = 0; k < a.values.size(); k += stride)
    {
        juce::StringArray row { formatNumber(a.values[k], degree + "C", 4) };
        for (const auto& tr : traces) row.add(formatNumber(tr.y[k], tr.unit, 5));
        t.rows.push_back(row);
    }
    addLinePlots(r, "Temperature sweep", "Temperature", degree + "C", false, traces);
    juce::StringArray drift;
    for (const auto& tr : traces)
        if (tr.y.size() > 1)
            drift.add(tr.name + " " + formatNumber((tr.y.back() - tr.y.front()) / (to - from), tr.unit + "/" + degree + "C", 3));
    r.tables.push_back(t);
    r.summary = "Operating point from " + formatNumber(from, degree + "C", 3) + " to " + formatNumber(to, degree + "C", 3)
              + ". Average drift: " + drift.joinIntoString(", ") + ".";
    return true;
}

bool runMonteCarlo(const Netlist& n, const Settings& s, Result& r)
{
    const auto metric = text(s, "metric", "DC voltage");
    const auto outName = text(s, "output", n.nets.empty() ? juce::String() : n.nets.front().name);
    const auto out = nodeOf(n, outName, r.error);
    if (out < 0) return false;
    const auto ref = nodeOf(n, text(s, "reference", "GND"), r.error);
    if (ref < 0) return false;
    int input = -1;
    if (metric != "DC voltage")
    {
        input = sourceOf(n, text(s, "input"), r.error);
        if (input < 0) return false;
    }
    const auto runs = juce::jlimit(2, 10000, integer(s, "runs", 200));
    const bool gaussian = text(s, "distribution", "Gaussian") == "Gaussian";
    const auto tolR = number(s, "tol_r", 5.0) / 100.0, tolC = number(s, "tol_c", 10.0) / 100.0;
    const auto tolL = number(s, "tol_l", 10.0) / 100.0, tolBeta = number(s, "tol_beta", 0.0) / 100.0;
    const auto frequency = number(s, "frequency", 1e3);
    const Settings acSettings { { "start", text(s, "ac_start", "10") }, { "stop", text(s, "ac_stop", "1meg") }, { "points", "40" } };
    juce::String freqError;
    const auto freqs = acFrequencies(acSettings, freqError);
    const auto base = circuit_sim::atTemperature(n.circuit, number(s, "temperature", circuit_sim::nominalTemperatureC));
    juce::String unit = metric == "DC voltage" ? "V" : metric == "AC gain (dB)" ? "dB" : metric == "Transfer-function gain" ? "V/V" : "Hz";

    auto evaluate = [&](const circuit_sim::Circuit& c, double& value) {
        if (metric == "DC voltage")
        {
            const auto op = circuit_sim::solveOperatingPoint(c);
            if (!op.ok) return false;
            value = op.voltages[(size_t)out] - op.voltages[(size_t)ref];
            return true;
        }
        if (metric == "Transfer-function gain")
        {
            const auto tf = circuit_sim::solveTransferFunction(c, out, ref, input);
            value = tf.gain;
            return tf.ok;
        }
        const auto driven = drivenBy(c, input);
        if (metric == "AC gain (dB)")
        {
            const auto ac = circuit_sim::solveAcAt(driven, { frequency });
            if (!ac.ok) return false;
            value = 20.0 * std::log10(std::max(1e-30, std::abs(ac.voltages[0][(size_t)out] - ac.voltages[0][(size_t)ref])));
            return true;
        }
        const auto ac = circuit_sim::solveAcAt(driven, freqs);
        if (!ac.ok) return false;
        Trace t;
        t.x = ac.frequency;
        for (const auto& v : ac.voltages) t.y.push_back(20.0 * std::log10(std::max(1e-30, std::abs(v[(size_t)out] - v[(size_t)ref]))));
        const auto m = quick(metric == "AC upper -3 dB corner" ? signal_measure::Kind::UpperCorner3dB : signal_measure::Kind::LowerCorner3dB, t);
        value = m.value;
        return m.ok;
    };

    double nominal = 0.0;
    if (!evaluate(base, nominal)) { r.error = "The nominal circuit does not give a value for " + metric + "."; return false; }
    std::mt19937 rng((unsigned)juce::jmax(0, integer(s, "seed", 1)));
    std::normal_distribution<double> normal(0.0, 1.0);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    auto spread = [&](double tolerance) { return 1.0 + (gaussian ? tolerance / 3.0 * normal(rng) : tolerance * uniform(rng)); };
    std::vector<double> values;
    int failed = 0;
    for (int run = 0; run < runs; ++run)
    {
        auto c = base;
        for (auto& e : c.elements())
        {
            if (isInternal(e)) continue;
            if (e.type == ElementType::Resistor && tolR > 0.0) e.value *= spread(tolR);
            if (e.type == ElementType::Capacitor && tolC > 0.0) e.value *= spread(tolC);
            if (e.type == ElementType::Inductor && tolL > 0.0) e.value *= spread(tolL);
            if ((e.type == ElementType::Npn || e.type == ElementType::Pnp) && tolBeta > 0.0) e.bjt.betaForward *= spread(tolBeta);
        }
        double value = 0.0;
        if (evaluate(c, value) && std::isfinite(value)) values.push_back(value);
        else ++failed;
    }
    if (values.size() < 2) { r.error = "Too few Monte Carlo runs gave a value."; return false; }
    double mean = 0.0;
    for (auto v : values) mean += v;
    mean /= (double)values.size();
    double var = 0.0;
    for (auto v : values) var += (v - mean) * (v - mean);
    const auto sigma = std::sqrt(var / (double)(values.size() - 1));
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());

    Plot hist;
    hist.kind = Plot::Kind::Histogram;
    hist.title = metric + " of " + "V(" + outName + "): " + juce::String((int)values.size()) + " runs";
    hist.xLabel = metric;
    hist.xUnit = unit;
    hist.yLabel = "Runs";
    Trace ht;
    ht.name = metric;
    ht.unit = "runs";
    const int bins = 24;
    const auto width = (*hi - *lo) > 0.0 ? (*hi - *lo) / bins : 1.0;
    std::vector<double> counts(bins, 0.0);
    for (auto v : values) counts[(size_t)juce::jlimit(0, bins - 1, (int)((v - *lo) / width))] += 1.0;
    for (int b = 0; b < bins; ++b) { ht.x.push_back(*lo + (b + 0.5) * width); ht.y.push_back(counts[(size_t)b]); }
    hist.traces.push_back(ht);
    r.plots.push_back(hist);

    Table stats;
    stats.title = "Statistics";
    stats.columns = { "Quantity", "Value" };
    stats.rows.push_back({ "Nominal", formatNumber(nominal, unit, 6) });
    stats.rows.push_back({ "Mean", formatNumber(mean, unit, 6) });
    stats.rows.push_back({ "Standard deviation", formatNumber(sigma, unit, 4) });
    stats.rows.push_back({ "Minimum", formatNumber(*lo, unit, 6) });
    stats.rows.push_back({ "Maximum", formatNumber(*hi, unit, 6) });
    stats.rows.push_back({ "Mean - 3 sigma", formatNumber(mean - 3.0 * sigma, unit, 6) });
    stats.rows.push_back({ "Mean + 3 sigma", formatNumber(mean + 3.0 * sigma, unit, 6) });
    stats.rows.push_back({ "Runs without a value", juce::String(failed) });
    r.tables.push_back(stats);
    Table runsTable;
    runsTable.title = "Runs";
    runsTable.columns = { "Run", metric };
    for (size_t i = 0; i < values.size() && i < 200; ++i)
        runsTable.rows.push_back({ juce::String((int)i + 1), formatNumber(values[i], unit, 6) });
    r.tables.push_back(runsTable);
    r.summary = juce::String(runs) + " runs (" + (gaussian ? "Gaussian, tolerance = 3 sigma" : "uniform") + "; R " + juce::String(tolR * 100.0, 3)
              + " %, C " + juce::String(tolC * 100.0, 3) + " %, L " + juce::String(tolL * 100.0, 3) + " %"
              + (tolBeta > 0.0 ? ", beta " + juce::String(tolBeta * 100.0, 3) + " %" : juce::String()) + "): mean "
              + formatNumber(mean, unit, 5) + ", sigma " + formatNumber(sigma, unit, 3) + ", range " + formatNumber(*lo, unit, 5) + " to " + formatNumber(*hi, unit, 5) + ".";
    return true;
}

// ---- field tables ---------------------------------------------------------------------

Field field(juce::String key, juce::String label, FieldKind kind, juce::String def, juce::String group,
            juce::String unit = {}, juce::String help = {}, juce::StringArray options = {})
{
    return { key, label, kind, unit, def, help, options, group };
}

void addStepFields(std::vector<Field>& f, const juce::String& label = "Parametric step (.STEP)")
{
    f.push_back(field("step_target", "Step", FieldKind::Target, "None", label, {}, "Part parameter (R1.value, V1.dc, Q1.beta) or TEMP; each value gives its own curve."));
    f.push_back(field("step_spacing", "Spacing", FieldKind::Choice, "List", label, {}, {}, { "List", "Linear", "Log" }));
    f.push_back(field("step_list", "Values", FieldKind::Text, "", label, {}, "For List: values separated by commas, e.g. 1k, 2.2k, 4.7k"));
    f.push_back(field("step_start", "From", FieldKind::Quantity, "", label));
    f.push_back(field("step_stop", "To", FieldKind::Quantity, "", label));
    f.push_back(field("step_count", "Count", FieldKind::Integer, "5", label));
}

Field temperatureField() { return field("temperature", "Temperature", FieldKind::Quantity, "27", "Setup", degree + "C", "Models are given at 27 " + degree + "C."); }
}

// ---- public -----------------------------------------------------------------------------

void addTraceStatsTable(Result& result, const std::vector<Trace>& traces, bool withFrequency)
{
    addTraceStatsTableInternal(result, traces, withFrequency);
}

const std::vector<AnalysisInfo>& analyses()
{
    static const std::vector<AnalysisInfo> list {
        { Analysis::OperatingPoint, "operating_point", "Operating Point (.OP)", "DC bias: every node voltage, every element current and power, and each transistor's operating region and small-signal parameters." },
        { Analysis::DcSweep, "dc_sweep", "DC Sweep (.DC)", "Sweep a source, a part value or temperature and plot node voltages and currents; optionally nested with a stepped second parameter." },
        { Analysis::Ac, "ac", "AC / Bode (.AC)", "Small-signal frequency response from an input source: magnitude, phase, group delay, -3 dB corners, unity gain and phase margin." },
        { Analysis::Transient, "transient", "Transient (.TRAN)", "Time-domain response to the sources' waveforms (sine, square, pulse, PWL, exponential)." },
        { Analysis::Fourier, "fourier", "Fourier / THD (.FOUR)", "Harmonic content and total harmonic distortion of a node's steady-state waveform." },
        { Analysis::Noise, "noise", "Noise (.NOISE)", "Thermal, shot and channel noise: output and input-referred densities, integrated totals, and each element's contribution." },
        { Analysis::TransferFunction, "transfer_function", "Transfer Function (.TF)", "Small-signal DC gain from a source to an output, with input and output resistance." },
        { Analysis::Sensitivity, "sensitivity", "Sensitivity (.SENS)", "How much an output (DC voltage or AC gain) moves for a 1 % change in every part parameter." },
        { Analysis::PoleZero, "pole_zero", "Pole-Zero (.PZ)", "Poles and zeros of the transfer function from a source to an output, with frequency, damping and Q." },
        { Analysis::Temperature, "temperature", "Temperature Sweep (.TEMP)", "Operating point across a temperature range: drift of every chosen node and current." },
        { Analysis::MonteCarlo, "monte_carlo", "Monte Carlo / Tolerance", "Random part spreads within tolerance: distribution, mean, standard deviation and worst cases of a chosen result." },
    };
    return list;
}

const AnalysisInfo* findAnalysis(const juce::String& key)
{
    for (const auto& a : analyses())
        if (a.key.equalsIgnoreCase(key.trim()))
            return &a;
    return nullptr;
}

const AnalysisInfo& infoFor(Analysis analysis)
{
    for (const auto& a : analyses())
        if (a.id == analysis)
            return a;
    return analyses().front();
}

std::vector<Field> fieldsFor(Analysis analysis)
{
    std::vector<Field> f;
    const auto outputs = field("outputs", "Outputs", FieldKind::Nets, "", "Outputs", {}, "Nets and currents, e.g. out, base, I(R1), I(V1). Empty: every net.");
    const auto output = field("output", "Output net", FieldKind::Net, "", "Setup");
    const auto reference = field("reference", "Reference net", FieldKind::Net, "GND", "Setup", {}, "The output is V(output) - V(reference).");
    const auto input = field("input", "Input source", FieldKind::Source, "", "Setup");
    switch (analysis)
    {
        case Analysis::OperatingPoint:
            f.push_back(temperatureField());
            break;
        case Analysis::DcSweep:
            f.push_back(field("sweep_target", "Sweep", FieldKind::Target, "", "Setup", {}, "Source (V1.dc), part value (R1.value) or TEMP."));
            f.push_back(field("sweep_spacing", "Spacing", FieldKind::Choice, "Linear", "Setup", {}, {}, { "Linear", "Log", "List" }));
            f.push_back(field("sweep_start", "From", FieldKind::Quantity, "0", "Setup"));
            f.push_back(field("sweep_stop", "To", FieldKind::Quantity, "10", "Setup"));
            f.push_back(field("sweep_count", "Points", FieldKind::Integer, "101", "Setup"));
            f.push_back(field("sweep_list", "Values (List)", FieldKind::Text, "", "Setup"));
            f.push_back(temperatureField());
            f.push_back(outputs);
            addStepFields(f, "Second parameter (nested)");
            break;
        case Analysis::Ac:
            f.push_back(input);
            f.push_back(field("start", "Start frequency", FieldKind::Quantity, "1", "Setup", "Hz"));
            f.push_back(field("stop", "Stop frequency", FieldKind::Quantity, "10meg", "Setup", "Hz"));
            f.push_back(field("spacing", "Spacing", FieldKind::Choice, "Decade", "Setup", {}, {}, { "Decade", "Linear" }));
            f.push_back(field("points", "Points (per decade, or total)", FieldKind::Integer, "50", "Setup"));
            f.push_back(field("magnitude", "Magnitude", FieldKind::Choice, "dB", "Setup", {}, {}, { "dB", "Linear" }));
            f.push_back(temperatureField());
            f.push_back(outputs);
            addStepFields(f);
            break;
        case Analysis::Transient:
            f.push_back(field("stop", "Stop time", FieldKind::Quantity, "auto", "Setup", "s", "auto: ten periods of the slowest source."));
            f.push_back(field("step", "Time step", FieldKind::Quantity, "auto", "Setup", "s", "auto: stop/2000, at most 1/100 of the fastest period; source corners are always hit exactly."));
            f.push_back(field("start", "Store from", FieldKind::Quantity, "0", "Setup", "s"));
            f.push_back(field("max_points", "Max stored points", FieldKind::Integer, "4000", "Setup"));
            f.push_back(temperatureField());
            f.push_back(outputs);
            f.push_back(field("compare_abs_tol", "Abs tolerance", FieldKind::Quantity, "1m", "Comparison",
                              {}, "Internal-vs-Xyce transient comparison tolerance in the trace unit."));
            f.push_back(field("compare_rel_tol", "Rel tolerance", FieldKind::Quantity, "0.01", "Comparison",
                              {}, "Internal-vs-Xyce relative tolerance. 0.01 means 1%; 1% is also accepted."));
            addStepFields(f);
            break;
        case Analysis::Fourier:
            f.push_back(output);
            f.push_back(field("fundamental", "Fundamental", FieldKind::Quantity, "auto", "Setup", "Hz", "auto: the slowest source frequency."));
            f.push_back(field("harmonics", "Harmonics", FieldKind::Integer, "9", "Setup"));
            f.push_back(field("periods", "Periods analysed", FieldKind::Integer, "1", "Setup"));
            f.push_back(field("settle_periods", "Settling periods first", FieldKind::Integer, "10", "Setup"));
            f.push_back(temperatureField());
            break;
        case Analysis::Noise:
            f.push_back(output);
            f.push_back(reference);
            f.push_back(input);
            f.push_back(field("start", "Start frequency", FieldKind::Quantity, "10", "Setup", "Hz"));
            f.push_back(field("stop", "Stop frequency", FieldKind::Quantity, "100k", "Setup", "Hz"));
            f.push_back(field("points", "Points per decade", FieldKind::Integer, "20", "Setup"));
            f.push_back(temperatureField());
            addStepFields(f);
            break;
        case Analysis::TransferFunction:
            f.push_back(output);
            f.push_back(reference);
            f.push_back(input);
            f.push_back(temperatureField());
            break;
        case Analysis::Sensitivity:
            f.push_back(field("mode", "Output quantity", FieldKind::Choice, "DC", "Setup", {}, "DC: the output voltage. AC: the gain in dB at the frequency.", { "DC", "AC" }));
            f.push_back(output);
            f.push_back(reference);
            f.push_back(field("input", "Input source (AC)", FieldKind::Source, "", "Setup"));
            f.push_back(field("frequency", "Frequency (AC)", FieldKind::Quantity, "1k", "Setup", "Hz"));
            f.push_back(temperatureField());
            break;
        case Analysis::PoleZero:
            f.push_back(input);
            f.push_back(output);
            f.push_back(reference);
            f.push_back(temperatureField());
            break;
        case Analysis::Temperature:
            f.push_back(field("temp_start", "From", FieldKind::Quantity, "-40", "Setup", degree + "C"));
            f.push_back(field("temp_stop", "To", FieldKind::Quantity, "125", "Setup", degree + "C"));
            f.push_back(field("temp_points", "Points", FieldKind::Integer, "34", "Setup"));
            f.push_back(outputs);
            break;
        case Analysis::MonteCarlo:
            f.push_back(field("metric", "Result", FieldKind::Choice, "DC voltage", "Setup", {}, {},
                              { "DC voltage", "Transfer-function gain", "AC gain (dB)", "AC upper -3 dB corner", "AC lower -3 dB corner" }));
            f.push_back(output);
            f.push_back(reference);
            f.push_back(field("input", "Input source (AC / gain)", FieldKind::Source, "", "Setup"));
            f.push_back(field("frequency", "Frequency (AC gain)", FieldKind::Quantity, "1k", "Setup", "Hz"));
            f.push_back(field("ac_start", "Corner search from", FieldKind::Quantity, "1", "Setup", "Hz"));
            f.push_back(field("ac_stop", "Corner search to", FieldKind::Quantity, "10meg", "Setup", "Hz"));
            f.push_back(field("runs", "Runs", FieldKind::Integer, "200", "Setup"));
            f.push_back(field("seed", "Random seed", FieldKind::Integer, "1", "Setup"));
            f.push_back(field("distribution", "Distribution", FieldKind::Choice, "Gaussian", "Tolerances", {}, "Gaussian: the tolerance is 3 sigma.", { "Gaussian", "Uniform" }));
            f.push_back(field("tol_r", "Resistors", FieldKind::Quantity, "5", "Tolerances", "%"));
            f.push_back(field("tol_c", "Capacitors", FieldKind::Quantity, "10", "Tolerances", "%"));
            f.push_back(field("tol_l", "Inductors", FieldKind::Quantity, "10", "Tolerances", "%"));
            f.push_back(field("tol_beta", "Transistor beta", FieldKind::Quantity, "0", "Tolerances", "%"));
            f.push_back(temperatureField());
            break;
    }
    return f;
}

juce::StringArray netChoices(const Netlist& netlist)
{
    juce::StringArray names { "GND" };
    for (const auto& net : netlist.nets) names.add(net.name);
    return names;
}

juce::StringArray sourceChoices(const Netlist& netlist)
{
    juce::StringArray names;
    for (const auto& p : netlist.parts)
        if (p.element >= 0)
        {
            const auto t = netlist.circuit.elements()[(size_t)p.element].type;
            if (t == ElementType::VoltageSource || t == ElementType::CurrentSource)
                names.add(p.refdes);
        }
    return names;
}

juce::StringArray targetChoices(const Netlist& netlist, bool includeTemperature)
{
    juce::StringArray names;
    for (const auto& p : netlist.parts)
        if (p.element >= 0)
            for (const auto& param : circuit_sim::parameterNames(netlist.circuit.elements()[(size_t)p.element].type))
                names.add(p.refdes + "." + juce::String(param));
    if (includeTemperature) names.add("TEMP");
    return names;
}

Result run(Analysis analysis, const Settings& given, const Netlist& netlist)
{
    Result r;
    r.analysis = analysis;
    r.title = infoFor(analysis).title;
    r.when = juce::Time::getCurrentTime();
    r.warnings = netlist.warnings;
    if (netlist.error.isNotEmpty())
    {
        r.error = netlist.error;
        return r;
    }
    Settings s = given;
    for (const auto& f : fieldsFor(analysis))
        if (s.find(f.key) == s.end() || s[f.key].trim().isEmpty())
        {
            auto def = f.defaultValue;
            if (def.isEmpty() && f.kind == FieldKind::Source && !sourceChoices(netlist).isEmpty()) def = sourceChoices(netlist)[0];
            if (def.isEmpty() && f.kind == FieldKind::Net && !netlist.nets.empty()) def = netlist.nets.front().name;
            if (def.isEmpty() && f.key == "sweep_target" && !sourceChoices(netlist).isEmpty()) def = sourceChoices(netlist)[0] + ".dc";
            s[f.key] = def;
        }
    r.settings = s;
    if (netlist.circuit.elements().empty())
    {
        r.error = "The diagram has nothing to simulate.";
        return r;
    }
    for (const auto& e : netlist.circuit.elements())
        if (e.type == ElementType::BehavioralVoltageSource || e.type == ElementType::BehavioralCurrentSource)
        {
            r.error = juce::String(e.name) + " is a Xyce behavioral source. Select the Xyce engine; the internal solver does not support equation-driven sources.";
            return r;
        }
        else if ((e.type == ElementType::VoltageControlledSwitch || e.type == ElementType::CurrentControlledSwitch)
                 && std::abs(e.hysteresis) > 0.0)
        {
            r.error = juce::String(e.name) + " uses switch hysteresis. Select the Xyce engine; the internal solver supports threshold switching only.";
            return r;
        }
    if (analysis == Analysis::Transient && !netlist.circuit.nodeInitialVoltages().empty())
    {
        r.error = "Explicit node-voltage initial conditions require the Xyce engine; the internal solver supports capacitor voltage and inductor current initial conditions only.";
        return r;
    }
    const auto started = juce::Time::getMillisecondCounterHiRes();
    switch (analysis)
    {
        case Analysis::OperatingPoint: r.ok = runOperatingPoint(netlist, s, r); break;
        case Analysis::DcSweep: r.ok = runDcSweep(netlist, s, r); break;
        case Analysis::Ac: r.ok = runAc(netlist, s, r); break;
        case Analysis::Transient: r.ok = runTransient(netlist, s, r); break;
        case Analysis::Fourier: r.ok = runFourier(netlist, s, r); break;
        case Analysis::Noise: r.ok = runNoise(netlist, s, r); break;
        case Analysis::TransferFunction: r.ok = runTransferFunction(netlist, s, r); break;
        case Analysis::Sensitivity: r.ok = runSensitivity(netlist, s, r); break;
        case Analysis::PoleZero: r.ok = runPoleZero(netlist, s, r); break;
        case Analysis::Temperature: r.ok = runTemperature(netlist, s, r); break;
        case Analysis::MonteCarlo: r.ok = runMonteCarlo(netlist, s, r); break;
    }
    r.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    if (!r.ok && r.error.isEmpty())
        r.error = "The analysis failed.";
    return r;
}

const Trace* findTrace(const Result& result, const juce::String& name, int* plotIndex)
{
    for (size_t p = 0; p < result.plots.size(); ++p)
        for (const auto& t : result.plots[p].traces)
            if (t.name.equalsIgnoreCase(name.trim()))
            {
                if (plotIndex != nullptr) *plotIndex = (int)p;
                return &t;
            }
    // Allow the bare output name when a trace has a step suffix or is the magnitude of a pair.
    for (size_t p = 0; p < result.plots.size(); ++p)
        for (const auto& t : result.plots[p].traces)
            if (t.name.upToFirstOccurrenceOf("  [", false, false).equalsIgnoreCase(name.trim()))
            {
                if (plotIndex != nullptr) *plotIndex = (int)p;
                return &t;
            }
    return nullptr;
}

juce::StringArray traceNames(const Result& result)
{
    juce::StringArray names;
    for (const auto& p : result.plots)
        if (p.kind == Plot::Kind::Lines)
            for (const auto& t : p.traces)
                names.addIfNotAlreadyThere(t.name);
    return names;
}

signal_measure::Result measure(const Result& result, const juce::String& traceName, const signal_measure::Request& request)
{
    int plotIndex = -1;
    const auto* trace = findTrace(result, traceName, &plotIndex);
    if (trace == nullptr)
    {
        signal_measure::Result r;
        r.error = "No trace named " + traceName.toStdString() + " in the last " + result.title.toStdString() + " result.";
        return r;
    }
    std::vector<double> phase;
    if (trace->partnerPlot >= 0 && trace->partnerPlot < (int)result.plots.size() && trace->partnerTrace >= 0
        && trace->partnerTrace < (int)result.plots[(size_t)trace->partnerPlot].traces.size())
        phase = result.plots[(size_t)trace->partnerPlot].traces[(size_t)trace->partnerTrace].y;
    return signal_measure::measure(request, trace->x, trace->y, phase);
}

juce::String toCsv(const Plot& plot)
{
    juce::String csv;
    if (plot.kind == Plot::Kind::PoleZero)
    {
        csv << "kind,real_rad_s,imag_rad_s\n";
        for (const auto& p : plot.poles) csv << "pole," << juce::String(p.real(), 9) << "," << juce::String(p.imag(), 9) << "\n";
        for (const auto& z : plot.zeros) csv << "zero," << juce::String(z.real(), 9) << "," << juce::String(z.imag(), 9) << "\n";
        return csv;
    }
    bool shared = !plot.traces.empty();
    for (const auto& t : plot.traces)
        shared = shared && t.x == plot.traces.front().x;
    auto quoted = [](const juce::String& s) { return "\"" + s.replace("\"", "'") + "\""; };
    if (shared)
    {
        csv << quoted(plot.xLabel + (plot.xUnit.isNotEmpty() ? " (" + plot.xUnit + ")" : juce::String()));
        for (const auto& t : plot.traces) csv << "," << quoted(t.name + (t.unit.isNotEmpty() ? " (" + t.unit + ")" : juce::String()));
        csv << "\n";
        for (size_t k = 0; k < plot.traces.front().x.size(); ++k)
        {
            csv << (plot.categories.size() > (int)k ? quoted(plot.categories[(int)k]) : juce::String(plot.traces.front().x[k], 9));
            for (const auto& t : plot.traces) csv << "," << juce::String(t.y[k], 9);
            csv << "\n";
        }
        return csv;
    }
    csv << "trace,x,y\n";
    for (const auto& t : plot.traces)
        for (size_t k = 0; k < t.x.size(); ++k)
            csv << quoted(t.name) << "," << juce::String(t.x[k], 9) << "," << juce::String(t.y[k], 9) << "\n";
    return csv;
}

juce::String toCsv(const Table& table)
{
    auto quoted = [](const juce::String& s) { return "\"" + s.replace("\"", "'") + "\""; };
    juce::String csv;
    juce::StringArray header;
    for (const auto& c : table.columns) header.add(quoted(c));
    csv << header.joinIntoString(",") << "\n";
    for (const auto& row : table.rows)
    {
        juce::StringArray cells;
        for (const auto& c : row) cells.add(quoted(c));
        csv << cells.joinIntoString(",") << "\n";
    }
    return csv;
}

juce::String toJson(const Result& r, const juce::StringArray& files)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("ok", r.ok);
    root->setProperty("analysis", infoFor(r.analysis).key);
    root->setProperty("title", r.title);
    if (!r.ok) root->setProperty("error", r.error);
    root->setProperty("summary", r.summary);
    root->setProperty("seconds", r.seconds);
    auto* settings = new juce::DynamicObject();
    for (const auto& [k, v] : r.settings) settings->setProperty(juce::Identifier(k), v);
    root->setProperty("settings", juce::var(settings));
    juce::Array<juce::var> warnings;
    for (const auto& w : r.warnings) warnings.add(w);
    root->setProperty("warnings", warnings);
    juce::Array<juce::var> tables;
    for (const auto& t : r.tables)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("title", t.title);
        juce::Array<juce::var> columns;
        for (const auto& c : t.columns) columns.add(c);
        o->setProperty("columns", columns);
        juce::Array<juce::var> rows;
        for (size_t i = 0; i < t.rows.size() && i < 80; ++i)
        {
            juce::Array<juce::var> cells;
            for (const auto& c : t.rows[i]) cells.add(c);
            rows.add(cells);
        }
        o->setProperty("rows", rows);
        if (t.rows.size() > 80) o->setProperty("rowsOmitted", (int)t.rows.size() - 80);
        tables.add(juce::var(o));
    }
    root->setProperty("tables", tables);
    juce::Array<juce::var> plots;
    for (const auto& p : r.plots)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("title", p.title);
        o->setProperty("x", p.xLabel + (p.xUnit.isNotEmpty() ? " (" + p.xUnit + ")" : juce::String()));
        o->setProperty("y", p.yLabel + (p.yUnit.isNotEmpty() ? " (" + p.yUnit + ")" : juce::String()));
        juce::Array<juce::var> traces;
        for (const auto& t : p.traces)
        {
            auto* to = new juce::DynamicObject();
            to->setProperty("name", t.name);
            to->setProperty("unit", t.unit);
            to->setProperty("points", (int)t.x.size());
            if (!t.y.empty())
            {
                const auto [lo, hi] = std::minmax_element(t.y.begin(), t.y.end());
                to->setProperty("min", *lo);
                to->setProperty("max", *hi);
                juce::Array<juce::var> samples;
                const auto stride = std::max<size_t>(1, t.x.size() / 16);
                for (size_t k = 0; k < t.x.size(); k += stride)
                    samples.add(juce::Array<juce::var> { t.x[k], t.y[k] });
                to->setProperty("samples", samples);
            }
            traces.add(juce::var(to));
        }
        o->setProperty("traces", traces);
        plots.add(juce::var(o));
    }
    root->setProperty("plots", plots);
    juce::Array<juce::var> fileList;
    for (const auto& f : files) fileList.add(f);
    root->setProperty("files", fileList);
    return juce::JSON::toString(juce::var(root), true);
}

juce::String formatNumber(double value, const juce::String& unit, int significant)
{
    if (std::isnan(value))
        return "NaN";
    if (std::abs(value) < 1e-18)
        value = 0.0; // below anything the solver resolves
    if (std::isinf(value))
        return juce::String(value > 0 ? "" : "-") + juce::String(juce::CharPointer_UTF8("\xe2\x88\x9e")) + (unit.isNotEmpty() ? " " + unit : juce::String());
    if (unit.isEmpty() || unit == "dB" || unit == "dBc" || unit == degree || unit == "%" || unit.startsWith(degree))
    {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.*g", significant, value);
        const juce::String s(buffer);
        return unit.isEmpty() ? s : s + " " + unit;
    }
    auto u = unit == "ohm" ? ohm : unit;
    return juce::String(circuit_sim::formatValue(value, u.toStdString(), significant));
}
}

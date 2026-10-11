#include "LogicGates.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace logic
{
namespace
{
struct Named { Function f; const char* name; };
constexpr Named names[] { { Function::Not, "not" }, { Function::Buffer, "buffer" }, { Function::And, "and" }, { Function::Nand, "nand" },
                          { Function::Or, "or" }, { Function::Nor, "nor" }, { Function::Xor, "xor" }, { Function::Xnor, "xnor" } };

bool inverting(Function f) { return f == Function::Not || f == Function::Nand || f == Function::Nor || f == Function::Xnor; }
}

bool parseFunction(const std::string& name, Function& out)
{
    std::string lower;
    for (const auto c : name) lower += (char)std::tolower((unsigned char)c);
    if (lower == "buf") lower = "buffer";
    if (lower == "inverter") lower = "not";
    for (const auto& n : names)
        if (lower == n.name) { out = n.f; return true; }
    return false;
}

const char* functionName(Function f)
{
    for (const auto& n : names)
        if (n.f == f) return n.name;
    return "?";
}

int minInputs(Function f) { return f == Function::Not || f == Function::Buffer ? 1 : 2; }
int maxInputs(Function f) { return f == Function::Not || f == Function::Buffer ? 1 : 8; }

double truth(Function f, const double* p, int count)
{
    double y = 0.0;
    switch (f)
    {
        case Function::Not:
        case Function::Buffer:
            y = count > 0 ? p[0] : 0.0;
            break;
        case Function::And:
        case Function::Nand:
            y = 1.0;
            for (int i = 0; i < count; ++i) y *= p[i];
            break;
        case Function::Or:
        case Function::Nor:
        {
            double none = 1.0;
            for (int i = 0; i < count; ++i) none *= 1.0 - p[i];
            y = 1.0 - none;
            break;
        }
        case Function::Xor:
        case Function::Xnor:
            // Odd parity: x XOR q = x + q - 2xq, exact at 0 and 1.
            y = count > 0 ? p[0] : 0.0;
            for (int i = 1; i < count; ++i) y = y + p[i] - 2.0 * y * p[i];
            break;
    }
    return inverting(f) ? 1.0 - y : y;
}

bool truth(Function f, const bool* levels, int count)
{
    std::array<double, 8> p {};
    for (int i = 0; i < count && i < 8; ++i) p[(size_t)i] = levels[i] ? 1.0 : 0.0;
    return truth(f, p.data(), std::min(count, 8)) > 0.5;
}

std::string problemsWith(const Model& m, Function f, int inputs)
{
    if (inputs < minInputs(f) || inputs > maxInputs(f))
        return std::string("a ") + functionName(f) + " gate has " + std::to_string(minInputs(f))
             + (maxInputs(f) == minInputs(f) ? "" : " to " + std::to_string(maxInputs(f))) + " input(s), not " + std::to_string(inputs);
    if (!(m.voh > m.vol)) return "the HIGH output level must be above the LOW level";
    if (!(m.vth > m.vol && m.vth < m.voh)) return "the input threshold must lie between the LOW and HIGH output levels";
    if (!(m.hysteresis >= 0.0) || m.vth - 0.5 * m.hysteresis <= m.vol || m.vth + 0.5 * m.hysteresis >= m.voh)
        return "the hysteresis must be at least 0 and keep both switching points between the LOW and HIGH levels";
    if (!(m.rOut > 0.0)) return "the output resistance must be greater than 0";
    if (!(m.rIn > 0.0)) return "the input resistance must be greater than 0";
    if (!(m.tpd > 0.0))
        return "the propagation delay must be greater than 0 (with no delay, gates in a feedback loop never settle)";
    if (!(m.tr >= 0.0)) return "the output transition time must be at least 0";
    if (!(m.dcGain > 0.0)) return "the DC gain must be greater than 0";
    return {};
}

GateDevice::GateDevice(Function f, int n, Model m) : function(f), inputs(n), model(m)
{
    scratchLevels.resize((size_t)n);
    reset();
}

void GateDevice::reset()
{
    committed = State {};
    committed.vin.assign((size_t)inputs, 0.0);
    committed.in.assign((size_t)inputs, 0);
    trial = committed;
}

bool GateDevice::logicOut(const std::vector<signed char>& in) const
{
    std::array<bool, 8> levels {};
    for (int i = 0; i < inputs; ++i) levels[(size_t)i] = in[(size_t)i] != 0;
    return truth(function, levels.data(), inputs);
}

double GateDevice::levelAt(const State& s, double t) const
{
    const auto to = s.target ? model.voh : model.vol;
    if (s.rampStart < 0.0 || model.tr <= 0.0 || t >= s.rampStart + model.tr)
        return to;
    const auto a = std::clamp((t - s.rampStart) / model.tr, 0.0, 1.0);
    return s.from + (to - s.from) * a;
}

// Starts the pending change when it is due for a step starting at `start`.
void GateDevice::startDue(State& s, double start) const
{
    if (s.pendingAt >= 0.0 && s.pendingAt <= start)
    {
        s.from = levelAt(s, s.pendingAt);
        s.rampStart = s.pendingAt;
        s.target = s.pendingTarget;
        s.pendingAt = -1.0;
    }
    if (s.rampStart >= 0.0 && start >= s.rampStart + model.tr)
        s.rampStart = -1.0; // settled
}

bool GateDevice::evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& currents, std::string& error)
{
    if ((int)v.size() != inputs + 1)
    {
        error = "a logic gate expects " + std::to_string(inputs + 1) + " pins";
        return false;
    }
    trial = committed;
    double level;
    if (committed.t < 0.0)
    {
        // DC: the smooth transfer curve, slope dcGain at the threshold for
        // one input (logistic with k = 4 gain / swing).
        const auto swing = model.voh - model.vol;
        const auto k = 4.0 * model.dcGain / swing;
        for (int i = 0; i < inputs; ++i)
            scratchLevels[(size_t)i] = 1.0 / (1.0 + std::exp(-k * (v[(size_t)i] - model.vth)));
        level = model.vol + swing * truth(function, scratchLevels.data(), inputs);
        trial.from = level;
    }
    else
    {
        startDue(trial, t - 0.5 * h);
        level = levelAt(trial, t);
    }
    for (int i = 0; i < inputs; ++i)
        trial.vin[(size_t)i] = v[(size_t)i];
    currents.resize((size_t)inputs + 1);
    for (int i = 0; i < inputs; ++i)
        currents[(size_t)i] = -v[(size_t)i] / model.rIn;
    currents[(size_t)inputs] = (level - v[(size_t)inputs]) / model.rOut;
    return true;
}

void GateDevice::accept(double t)
{
    auto next = trial;
    if (committed.t < 0.0)
    {
        // The first accepted solution (t = 0): input levels at the
        // threshold, and the output heads for its logic level from where
        // the DC solution left it (a transition when that was mid-way).
        for (int i = 0; i < inputs; ++i)
            next.in[(size_t)i] = next.vin[(size_t)i] > model.vth ? 1 : 0;
        next.target = logicOut(next.in);
        const auto to = next.target ? model.voh : model.vol;
        next.rampStart = std::abs(next.from - to) > 1e-9 && model.tr > 0.0 ? t : -1.0;
        next.pendingAt = -1.0;
        next.t = t;
        committed = next;
        trial = next;
        return;
    }
    // Input crossings in this step, in time order (ties by input order).
    struct Crossing { double t; int input; signed char level; };
    std::array<Crossing, 8> crossings {};
    int count = 0;
    const auto t0 = committed.t;
    for (int i = 0; i < inputs; ++i)
    {
        const auto was = committed.in[(size_t)i];
        const auto v0 = committed.vin[(size_t)i], v1 = next.vin[(size_t)i];
        const auto rise = model.vth + 0.5 * model.hysteresis, fall = model.vth - 0.5 * model.hysteresis;
        double point = 0.0;
        signed char level = was;
        if (was == 0 && v1 > rise) { level = 1; point = rise; }
        else if (was != 0 && v1 < fall) { level = 0; point = fall; }
        if (level == was)
            continue;
        const auto dv = v1 - v0;
        const auto tc = dv != 0.0 ? std::clamp(t0 + (t - t0) * (point - v0) / dv, t0, t) : t;
        crossings[(size_t)count++] = { tc, i, level };
    }
    std::sort(crossings.begin(), crossings.begin() + count,
              [](const Crossing& a, const Crossing& b) { return a.t != b.t ? a.t < b.t : a.input < b.input; });
    for (int k = 0; k < count; ++k)
    {
        const auto& c = crossings[(size_t)k];
        next.in[(size_t)c.input] = c.level;
        const bool out = logicOut(next.in);
        // Inertial: a new input change cancels a change not started yet.
        next.pendingAt = -1.0;
        if (out != next.target)
        {
            next.pendingAt = c.t + model.tpd;
            next.pendingTarget = out;
        }
    }
    next.t = t;
    committed = next;
    trial = next;
}

double GateDevice::nextEventTime(double after) const
{
    auto e = std::numeric_limits<double>::infinity();
    const auto& s = committed;
    if (s.pendingAt >= 0.0)
    {
        if (s.pendingAt > after) e = std::min(e, s.pendingAt);
        if (model.tr > 0.0 && s.pendingAt + model.tr > after) e = std::min(e, s.pendingAt + model.tr);
    }
    if (s.rampStart >= 0.0 && model.tr > 0.0 && s.rampStart + model.tr > after)
        e = std::min(e, s.rampStart + model.tr);
    return e;
}

bool GateDevice::saveState(std::vector<std::uint8_t>& out) const
{
    const auto& s = committed;
    std::vector<double> v { s.t, s.target ? 1.0 : 0.0, s.rampStart, s.from, s.pendingAt, s.pendingTarget ? 1.0 : 0.0 };
    for (int i = 0; i < inputs; ++i) v.push_back(s.vin[(size_t)i]);
    for (int i = 0; i < inputs; ++i) v.push_back(s.in[(size_t)i]);
    out.resize(v.size() * sizeof(double));
    std::memcpy(out.data(), v.data(), out.size());
    return true;
}

bool GateDevice::restoreState(const std::vector<std::uint8_t>& in)
{
    const auto n = (size_t)(6 + 2 * inputs);
    if (in.size() != n * sizeof(double))
        return false;
    std::vector<double> v(n);
    std::memcpy(v.data(), in.data(), in.size());
    State s;
    s.t = v[0]; s.target = v[1] != 0.0; s.rampStart = v[2]; s.from = v[3]; s.pendingAt = v[4]; s.pendingTarget = v[5] != 0.0;
    s.vin.resize((size_t)inputs);
    s.in.resize((size_t)inputs);
    for (int i = 0; i < inputs; ++i)
    {
        s.vin[(size_t)i] = v[6 + (size_t)i];
        s.in[(size_t)i] = v[6 + (size_t)inputs + (size_t)i] != 0.0 ? 1 : 0;
    }
    committed = trial = s;
    return true;
}
}

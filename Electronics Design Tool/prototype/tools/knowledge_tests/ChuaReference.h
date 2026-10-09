#pragma once

// Reference Chua circuits as the Workbench builds them, and a classifier for
// the simulated V(C1) waveform. Used to verify the values the knowledge cards
// document actually produce a double-scroll attractor in this simulator.

#include "../../Source/Analytics.h"
#include "../../Source/CircuitSolver.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace chua_reference
{
struct Values
{
    double c1 = 10e-9, c2 = 100e-9, l = 18e-3, r = 1.8e3; // Kennedy 1992
    double supply = 9.0;
    double initialC1 = 0.1; // volts, capacitor initial_voltage
};

struct Built
{
    analytics::Netlist netlist;
    int v1 = 0, v2 = 0;
};

// Chua's circuit: C1 and the nonlinear resistor NR on node v1, R from v1 to
// v2, C2 and L on node v2, all returned to ground.
inline Built core(const Values& v)
{
    Built b;
    auto& c = b.netlist.circuit;
    b.v1 = c.addNode();
    b.v2 = c.addNode();
    const auto c1 = c.addCapacitor("C1", b.v1, 0, v.c1);
    c.elements()[(size_t)c1].hasInitialCondition = true;
    c.elements()[(size_t)c1].initialCondition = v.initialC1;
    c.addCapacitor("C2", b.v2, 0, v.c2);
    c.addInductor("L1", b.v2, 0, v.l);
    c.addResistor("R", b.v1, b.v2, v.r);
    b.netlist.nets = { { "V1", b.v1, "C1.1 R.1" }, { "V2", b.v2, "C2.1 L1.1 R.2" } };
    return b;
}

// Kennedy's two-op-amp nonlinear resistor (Kennedy 1992), each op amp a
// negative impedance converter from v1 to ground:
//   A1: 220 ohm OUT->IN+, 220 ohm OUT->IN-, 2.2k IN- -> GND  (slope -1/2.2k)
//   A2: 22k OUT->IN+,     22k OUT->IN-,     3.3k IN- -> GND  (slope -1/3.3k, saturates first)
inline Built opAmpRealization(const Values& v)
{
    auto b = core(v);
    auto& c = b.netlist.circuit;
    const auto vp = c.addNode(), vn = c.addNode();
    circuit_sim::Waveform plus, minus;
    plus.offset = v.supply;
    minus.offset = -v.supply;
    c.addVoltageSource("VCC", vp, 0, plus);
    c.addVoltageSource("VEE", vn, 0, minus);
    circuit_sim::OpAmpModel m; // as opamp_generic: gain 100k, headroom 0
    m.gain = 1e5;
    m.railDrop = 0.0;
    auto nic = [&](const char* name, double feedback, double toMinus, double toGround) {
        const auto out = c.addNode(), inMinus = c.addNode();
        c.addOpAmp(name, b.v1, inMinus, out, vp, vn, m);
        c.addResistor(std::string(name) + ".Rf", out, b.v1, feedback);
        c.addResistor(std::string(name) + ".Ra", out, inMinus, toMinus);
        c.addResistor(std::string(name) + ".Rg", inMinus, 0, toGround);
    };
    nic("A1", 220.0, 220.0, 2.2e3);
    nic("A2", 22e3, 22e3, 3.3e3);
    return b;
}

// The same characteristic as a piecewise-linear Xyce behavioral current source
// from v1 to ground: i = Gb*v + 0.5*(Ga-Gb)*(|v+E| - |v-E|).
inline juce::String behavioralExpression(const juce::String& node, double ga, double gb, double e)
{
    const auto v = "V(" + node + ")";
    return juce::String(gb, 9) + "*" + v + "+0.5*(" + juce::String(ga - gb, 9) + ")*(abs(" + v + "+" + juce::String(e, 6)
         + ")-abs(" + v + "-" + juce::String(e, 6) + "))";
}

inline Built behavioralRealization(const Values& v, double ga = -0.757e-3, double gb = -0.409e-3, double e = 1.0)
{
    auto b = core(v);
    b.netlist.circuit.addBehavioralCurrentSource("NR", b.v1, 0, behavioralExpression("V1", ga, gb, e).toStdString());
    return b;
}

enum class Behaviour { Unstable, Decayed, SingleScroll, Periodic, DoubleScroll };

inline const char* name(Behaviour b)
{
    switch (b)
    {
        case Behaviour::Unstable: return "unstable";
        case Behaviour::Decayed: return "decayed";
        case Behaviour::SingleScroll: return "single-scroll";
        case Behaviour::Periodic: return "periodic";
        case Behaviour::DoubleScroll: return "double-scroll";
    }
    return "?";
}

struct Verdict
{
    Behaviour behaviour = Behaviour::Unstable;
    int switches = 0;      // jumps between the two scrolls
    double spread = 0.0;   // coefficient of variation of the time spent in a scroll
    double minV = 0.0, maxV = 0.0;
};

// Classifies V(C1) after `settle`. The two scrolls sit at opposite signs of
// V(C1); a jump is counted when it crosses to the other side by more than
// `hysteresis`. Chaos: many jumps at irregular intervals. A periodic orbit
// that visits both scrolls jumps at regular intervals.
inline Verdict classify(const std::vector<double>& t, const std::vector<double>& v1, double settle, double hysteresis = 0.5)
{
    Verdict out;
    out.minV = 1e300;
    out.maxV = -1e300;
    int side = 0;
    double lastJump = -1.0;
    std::vector<double> stays;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (!std::isfinite(v1[i]) || std::abs(v1[i]) > 50.0)
            return out; // unstable
        if (t[i] < settle)
            continue;
        out.minV = std::min(out.minV, v1[i]);
        out.maxV = std::max(out.maxV, v1[i]);
        const int now = v1[i] > hysteresis ? 1 : v1[i] < -hysteresis ? -1 : side;
        if (side != 0 && now != side)
        {
            ++out.switches;
            if (lastJump >= 0.0)
                stays.push_back(t[i] - lastJump);
            lastJump = t[i];
        }
        side = now;
    }
    if (out.maxV - out.minV < 0.1)
    {
        out.behaviour = Behaviour::Decayed;
        return out;
    }
    if (out.switches < 2)
    {
        out.behaviour = Behaviour::SingleScroll;
        return out;
    }
    double mean = 0.0, var = 0.0;
    for (auto s : stays) mean += s;
    mean /= std::max<size_t>(1, stays.size());
    for (auto s : stays) var += (s - mean) * (s - mean);
    out.spread = stays.size() > 1 ? std::sqrt(var / (double)stays.size()) / mean : 0.0;
    out.behaviour = out.switches >= 8 && out.spread > 0.25 ? Behaviour::DoubleScroll : Behaviour::Periodic;
    return out;
}
}

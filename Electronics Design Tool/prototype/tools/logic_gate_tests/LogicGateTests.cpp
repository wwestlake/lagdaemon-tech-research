// Logic gates (Source/LogicGates.*, in circuit_sim) on the internal solver:
// truth tables, configurable inputs, levels and drive, thresholds and
// hysteresis, propagation timing on the stepper's device events, inertial
// delay, feedback (ring and Schmitt oscillators), SR and clocked latches,
// rollback, and the model's own checks.
//
// Built twice, against the optimised and the unoptimised circuit_sim. Each
// run writes its measured values to an output folder; `--compare A B`
// checks the two configurations agree (1e-9 relative + 1e-12 absolute).
//
// Usage: logic_gate_tests [--out <dir>]
//        logic_gate_tests --compare <dirA> <dirB>

#include "../../Source/CircuitSolver.h"
#include "../../Source/LogicGates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef LOGIC_GATE_TESTS_CONFIG
#define LOGIC_GATE_TESTS_CONFIG "unknown"
#endif

using namespace circuit_sim;
namespace fs = std::filesystem;

namespace
{
int failures = 0, passes = 0;
std::string outDir = "logic_gate_test_out";

void check(bool ok, const std::string& name, const std::string& detail = {})
{
    if (ok) ++passes; else ++failures;
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), ok || detail.empty() ? "" : "  -- ", ok ? "" : detail.c_str());
    std::fflush(stdout);
}

std::string fmt(const char* f, double v)
{
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

bool sameBits(const std::vector<double>& a, const std::vector<double>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

void writeValues(const std::string& name, const std::vector<double>& values)
{
    std::ofstream f(fs::path(outDir) / (name + ".txt"));
    char b[40];
    for (auto v : values)
    {
        std::snprintf(b, sizeof b, "%.17g\n", v);
        f << b;
    }
}

Waveform dc(double v)
{
    Waveform w;
    w.offset = v;
    return w;
}

Waveform pwl(std::vector<std::pair<double, double>> points)
{
    Waveform w;
    w.kind = Waveform::Kind::Pwl;
    w.points = std::move(points);
    return w;
}

// 0 -> 5 V linearly from `at` over `rise`, then held.
Waveform rampAt(double at, double rise)
{
    return pwl({ { 0.0, 0.0 }, { at, 0.0 }, { at + rise, 5.0 } });
}

std::shared_ptr<logic::GateDevice> gate(logic::Function f, int inputs, logic::Model m = {})
{
    return std::make_shared<logic::GateDevice>(f, inputs, m);
}

int addGate(Circuit& c, const std::string& name, logic::Function f, std::vector<Node> ins, Node out, logic::Model m = {})
{
    auto pins = ins;
    pins.push_back(out);
    return c.addProgrammable(name, pins, gate(f, (int)ins.size(), m));
}

// Linear interpolation of a node's transient voltage at t.
double at(const TransientResult& r, Node n, double t)
{
    for (size_t i = 1; i < r.time.size(); ++i)
        if (r.time[i] >= t)
        {
            const auto t0 = r.time[i - 1], t1 = r.time[i];
            const auto v0 = r.voltages[i - 1][(size_t)n], v1 = r.voltages[i][(size_t)n];
            return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
        }
    return r.voltages.empty() ? 0.0 : r.voltages.back()[(size_t)n];
}

// Times the node crosses `level` (interpolated), rising (+1), falling (-1) or both (0).
std::vector<double> crossings(const TransientResult& r, Node n, double level, int direction, double after = 0.0)
{
    std::vector<double> out;
    for (size_t i = 1; i < r.time.size(); ++i)
    {
        const auto v0 = r.voltages[i - 1][(size_t)n] - level, v1 = r.voltages[i][(size_t)n] - level;
        const bool up = v0 < 0.0 && v1 >= 0.0, down = v0 > 0.0 && v1 <= 0.0;
        if ((up && direction >= 0) || (down && direction <= 0))
        {
            const auto t = r.time[i - 1] + (r.time[i] - r.time[i - 1]) * (-v0) / (v1 - v0);
            if (t > after) out.push_back(t);
        }
    }
    return out;
}

// ---- 1. the logical function ------------------------------------------------------

void testTruthTables()
{
    std::printf("-- truth tables (one shared function) --\n");
    using F = logic::Function;
    const F all[] { F::Not, F::Buffer, F::And, F::Nand, F::Or, F::Nor, F::Xor, F::Xnor };
    int wrong = 0, rows = 0;
    std::string detail;
    for (const auto f : all)
        for (int n = logic::minInputs(f); n <= logic::maxInputs(f); ++n)
            for (int bits = 0; bits < (1 << n); ++bits)
            {
                bool in[8] {};
                double soft[8] {};
                int ones = 0;
                for (int i = 0; i < n; ++i)
                {
                    in[i] = (bits >> i) & 1;
                    soft[i] = in[i] ? 1.0 : 0.0;
                    ones += in[i];
                }
                bool expected = false;
                switch (f)
                {
                    case F::Not: expected = !in[0]; break;
                    case F::Buffer: expected = in[0]; break;
                    case F::And: expected = ones == n; break;
                    case F::Nand: expected = ones != n; break;
                    case F::Or: expected = ones > 0; break;
                    case F::Nor: expected = ones == 0; break;
                    case F::Xor: expected = ones % 2 == 1; break;
                    case F::Xnor: expected = ones % 2 == 0; break;
                }
                const auto y = logic::truth(f, soft, n);
                ++rows;
                if (logic::truth(f, in, n) != expected || y != (expected ? 1.0 : 0.0))
                {
                    ++wrong;
                    detail = std::string(logic::functionName(f)) + " n=" + std::to_string(n) + " bits=" + std::to_string(bits);
                }
            }
    check(wrong == 0, "NOT, BUFFER and AND/NAND/OR/NOR/XOR/XNOR with 2..8 inputs: " + std::to_string(rows)
                      + " rows match (boolean and soft at 0/1 exactly)", detail);
    // Between 0 and 1 the soft function is the multilinear extension: an AND
    // of 0.5 and 0.5 is 0.25, an XOR of 0.5 and anything is 0.5.
    const double half[] { 0.5, 0.5 }, mixed[] { 0.5, 0.9 };
    check(std::abs(logic::truth(F::And, half, 2) - 0.25) < 1e-15 && std::abs(logic::truth(F::Xor, mixed, 2) - 0.5) < 1e-15
              && std::abs(logic::truth(F::Nor, half, 2) - 0.25) < 1e-15,
          "between 0 and 1: the multilinear extension (AND 0.25, XOR 0.5, NOR 0.25)");
    logic::Function f;
    check(logic::parseFunction("NAND", f) && f == F::Nand && logic::parseFunction("buf", f) && f == F::Buffer
              && logic::parseFunction("inverter", f) && f == F::Not && !logic::parseFunction("nandy", f),
          "names parse (case-insensitive, buf, inverter); unknown names do not");
}

// ---- 2. DC: truth tables in a circuit, levels and drive, the transfer curve --------

void testDcBehaviour(std::vector<double>& record)
{
    std::printf("-- DC: truth tables, output levels, drive, transfer curve --\n");
    // A 2-input NAND driven by sources, loaded with 10k: VOH x 10k / (10k + 50).
    const auto high = 5.0 * 10e3 / (10e3 + 50.0);
    bool ok = true;
    std::string detail;
    for (int bits = 0; bits < 4; ++bits)
    {
        Circuit c;
        const auto a = c.addNode(), b = c.addNode(), y = c.addNode();
        c.addVoltageSource("VA", a, 0, dc(bits & 1 ? 5.0 : 0.0));
        c.addVoltageSource("VB", b, 0, dc(bits & 2 ? 5.0 : 0.0));
        addGate(c, "U1", logic::Function::Nand, { a, b }, y);
        c.addResistor("RL", y, 0, 10e3);
        const auto op = solveOperatingPoint(c);
        const auto v = op.ok ? op.voltages[(size_t)y] : -1.0;
        const auto expected = bits == 3 ? 0.0 : high;
        record.push_back(v);
        if (!op.ok || std::abs(v - expected) > 1e-6)
        {
            ok = false;
            detail += " A=" + std::to_string(bits & 1) + " B=" + std::to_string((bits >> 1) & 1) + fmt(": %.9g V", v) + op.error;
        }
    }
    check(ok, fmt("NAND truth table at the operating point, 10k load: 1 1 -> 0 V, else %.6f V (VOH behind 50 ohm)", high), detail);

    // 3- and 4-input gates.
    ok = true;
    detail.clear();
    for (const auto f : { logic::Function::And, logic::Function::Or, logic::Function::Xor, logic::Function::Xnor })
        for (int n = 3; n <= 4; ++n)
            for (int bits = 0; bits < (1 << n); ++bits)
            {
                Circuit c;
                std::vector<Node> ins;
                bool levels[8] {};
                for (int i = 0; i < n; ++i)
                {
                    ins.push_back(c.addNode());
                    levels[i] = (bits >> i) & 1;
                    c.addVoltageSource("V" + std::to_string(i), ins.back(), 0, dc(levels[i] ? 5.0 : 0.0));
                }
                const auto y = c.addNode();
                addGate(c, "U1", f, ins, y);
                const auto op = solveOperatingPoint(c);
                const auto expected = logic::truth(f, levels, n) ? 5.0 : 0.0;
                if (!op.ok || std::abs(op.voltages[(size_t)y] - expected) > 1e-3)
                {
                    ok = false;
                    detail = std::string(logic::functionName(f)) + " n=" + std::to_string(n) + " bits " + std::to_string(bits);
                }
            }
    check(ok, "3- and 4-input AND, OR, XOR, XNOR: every row right at the operating point", detail);

    // Drive: a low output sinking current through a pull-up: V = 5 x 50 / (50 + 1k).
    {
        Circuit c;
        const auto a = c.addNode(), vcc = c.addNode(), y = c.addNode();
        c.addVoltageSource("VA", a, 0, dc(5.0));
        c.addVoltageSource("VCC", vcc, 0, dc(5.0));
        addGate(c, "U1", logic::Function::Buffer, { a }, y);
        c.addResistor("RL", vcc, y, 1e3);
        Circuit c2 = c;
        c2.elements()[0].wave = dc(0.0);
        const auto op = solveOperatingPoint(c2);
        const auto v = op.ok ? op.voltages[(size_t)y] : -1.0;
        check(op.ok && std::abs(v - 5.0 * 50.0 / 1050.0) < 1e-6, "a LOW output sinks a 1k pull-up: 238 mV (50 ohm output resistance)",
              fmt("%.9g V", v));
        record.push_back(v);
    }

    // The DC transfer curve of a buffer: mid-swing at the threshold, slope
    // dcGain there, at the rails half a volt away.
    {
        auto vtc = [](double vin) {
            Circuit c;
            const auto a = c.addNode(), y = c.addNode();
            c.addVoltageSource("VA", a, 0, dc(vin));
            addGate(c, "U1", logic::Function::Buffer, { a }, y);
            const auto op = solveOperatingPoint(c);
            return op.ok ? op.voltages[(size_t)y] : -1.0;
        };
        const auto mid = vtc(2.5), slope = (vtc(2.5 + 1e-4) - vtc(2.5 - 1e-4)) / 2e-4;
        const auto low = vtc(2.0), highV = vtc(3.0);
        check(std::abs(mid - 2.5) < 1e-6 && std::abs(slope - 25.0) < 0.01 && low < 5e-4 && highV > 5.0 - 5e-4,
              "DC transfer curve: 2.5 V at the threshold, slope 25 V/V there, within 0.5 mV of the rails at 2.0 and 3.0 V",
              fmt("mid %.6g", mid) + fmt(" slope %.6g", slope) + fmt(" low %.3g", low) + fmt(" high %.6g", highV));
        record.push_back(mid);
        record.push_back(slope);
    }
}

// ---- 3. transient: thresholds, hysteresis, exact timing, inertial delay ------------

void testTiming(std::vector<double>& record)
{
    std::printf("-- transient: thresholds, hysteresis, propagation and transition timing --\n");
    // Input ramp 0 -> 5 V over 5 us from 10 us (1 V/us), back down 25..30 us.
    // Hysteresis 1 V: rising switch at 3.0 V (13 us), falling at 2.0 V (28 us).
    // tpd 10 ns, tr 4 ns: the output starts 10 ns after each crossing and
    // completes 4 ns later.
    logic::Model m;
    m.hysteresis = 1.0;
    Circuit c;
    const auto a = c.addNode(), y = c.addNode();
    c.addVoltageSource("VA", a, 0, pwl({ { 0.0, 0.0 }, { 10e-6, 0.0 }, { 15e-6, 5.0 }, { 25e-6, 5.0 }, { 30e-6, 0.0 } }));
    addGate(c, "U1", logic::Function::Buffer, { a }, y, m);
    const auto r = solveTransient(c, 40e-6, 1e-6, {}, 1 << 20);
    auto sampleAt = [&r](double t) {
        for (size_t i = 0; i < r.time.size(); ++i)
            if (std::abs(r.time[i] - t) < 1e-15) return (long)i;
        return -1L;
    };
    const auto riseStart = sampleAt(13e-6 + 10e-9), riseEnd = sampleAt(13e-6 + 14e-9);
    const auto fallStart = sampleAt(28e-6 + 10e-9), fallEnd = sampleAt(28e-6 + 14e-9);
    const bool ok = r.ok && riseStart > 0 && riseEnd == riseStart + 1 && fallStart > 0 && fallEnd == fallStart + 1;
    check(ok, "hysteresis 1 V: rising at 3.0 V (13 us), falling at 2.0 V (28 us); each output change starts at +10 ns and ends at +14 ns, "
              "on samples at exactly those times", r.error);
    if (ok)
    {
        const auto& v = r.voltages;
        check(v[(size_t)riseStart][(size_t)y] < 1e-6 && v[(size_t)riseEnd][(size_t)y] > 5.0 - 1e-6
                  && v[(size_t)fallStart][(size_t)y] > 5.0 - 1e-6 && v[(size_t)fallEnd][(size_t)y] < 1e-6,
              "the output is at the old level when the change starts (left limit) and at the new level when it ends");
        const auto mid = crossings(r, y, 2.5, 0);
        check(mid.size() == 2 && std::abs(mid[0] - 13.012e-6) < 1e-15 && std::abs(mid[1] - 28.012e-6) < 1e-15,
              "the output passes 2.5 V half way through each transition: 13.012 us and 28.012 us",
              mid.size() == 2 ? fmt("%.17g", mid[0]) + fmt(" %.17g", mid[1]) : std::to_string(mid.size()) + " crossings");
        for (auto t : mid) record.push_back(t);
    }
    // No hysteresis: the threshold is 2.5 V exactly (12.5 us in).
    {
        Circuit c2;
        const auto a2 = c2.addNode(), y2 = c2.addNode();
        c2.addVoltageSource("VA", a2, 0, rampAt(10e-6, 5e-6));
        addGate(c2, "U1", logic::Function::Not, { a2 }, y2);
        const auto r2 = solveTransient(c2, 20e-6, 1e-6, {}, 1 << 20);
        const auto mid = crossings(r2, y2, 2.5, -1);
        check(r2.ok && mid.size() == 1 && std::abs(mid[0] - (12.5e-6 + 12e-9)) < 1e-15,
              "an inverter switching at 2.5 V: the output falls through 2.5 V at 12.5 us + 10 ns + 2 ns",
              mid.empty() ? r2.error : fmt("%.17g", mid[0]));
        if (!mid.empty()) record.push_back(mid[0]);
    }
    // Inertial delay: an input glitch shorter than tpd (5 ns) never reaches the
    // output; a longer one (20 ns) does, delayed.
    for (const double width : { 5e-9, 20e-9 })
    {
        Circuit c3;
        const auto a3 = c3.addNode(), y3 = c3.addNode();
        c3.addVoltageSource("VA", a3, 0, pwl({ { 0.0, 0.0 }, { 1e-6, 0.0 }, { 1e-6 + 1e-10, 5.0 }, { 1e-6 + 1e-10 + width, 5.0 },
                                               { 1e-6 + 2e-10 + width, 0.0 } }));
        addGate(c3, "U1", logic::Function::Buffer, { a3 }, y3);
        const auto r3 = solveTransient(c3, 2e-6, 0.1e-6, {}, 1 << 20);
        double peak = 0.0;
        for (const auto& v : r3.voltages) peak = std::max(peak, v[(size_t)y3]);
        if (width < 10e-9)
            check(r3.ok && peak < 1e-6, "inertial delay: a 5 ns input pulse (shorter than tpd 10 ns) does not reach the output", fmt("%.6g V", peak));
        else
            check(r3.ok && peak > 5.0 - 1e-6, "a 20 ns input pulse (longer than tpd) reaches the output", fmt("%.6g V", peak));
    }
}

// ---- 4. feedback: ring oscillator and Schmitt RC oscillator ------------------------

void testFeedback(std::vector<double>& record)
{
    std::printf("-- feedback: ring oscillator, Schmitt RC oscillator (analog + digital) --\n");
    {
        // NAND (enable) + two inverters in a ring, tpd 10 ns, tr 2 ns: each
        // stage switches when its input passes half way, so a stage takes
        // tpd + tr/2 = 11 ns and the period is 2 x 3 x 11 ns = 66 ns.
        logic::Model m;
        m.tr = 2e-9;
        Circuit c;
        const auto en = c.addNode(), n1 = c.addNode(), n2 = c.addNode(), n3 = c.addNode();
        c.addVoltageSource("VEN", en, 0, rampAt(100e-9, 1e-9));
        addGate(c, "U1", logic::Function::Nand, { en, n3 }, n1, m);
        addGate(c, "U2", logic::Function::Not, { n1 }, n2, m);
        addGate(c, "U3", logic::Function::Not, { n2 }, n3, m);
        TransientStepper s(c);
        const auto r = solveTransient(c, 2e-6, 5e-9, {}, 1 << 20);
        const auto rises = crossings(r, n1, 2.5, +1, 400e-9);
        bool ok = r.ok && rises.size() > 20;
        double worst = 0.0;
        for (size_t k = 1; ok && k < rises.size(); ++k)
            worst = std::max(worst, std::abs(rises[k] - rises[k - 1] - 66e-9));
        check(ok && worst < 1e-14, "ring oscillator (NAND + 2 NOT): period 66 ns exactly (2 x 3 x (tpd + tr/2)), "
                                   + std::to_string(rises.size()) + " cycles, no runaway", r.error + fmt(" worst %.3g s", worst));
        if (rises.size() > 2) record.push_back(rises[1] - rises[0]);
        // Before the enable the ring is held: n1 high, steady.
        check(r.ok && at(r, n1, 90e-9) > 5.0 - 1e-6 && crossings(r, n1, 2.5, 0, 0.0).front() > 100e-9,
              "with the enable low the ring is held (no oscillation before 100 ns)");
    }
    {
        // Schmitt inverter (thresholds 3 V and 2 V) with R from its output to
        // its input and C from its input to ground: the input charges between
        // 2 and 3 V through R + rOut; each half period is (R + rOut) C ln 1.5
        // plus tpd (the output changes 10 ns after the crossing).
        logic::Model m;
        m.hysteresis = 1.0;
        m.tr = 0.0;
        Circuit c;
        const auto in = c.addNode(), out = c.addNode();
        addGate(c, "U1", logic::Function::Not, { in }, out, m);
        c.addResistor("R1", out, in, 10e3);
        c.addCapacitor("C1", in, 0, 10e-9);
        const auto r = solveTransient(c, 500e-6, 0.1e-6, {}, 1 << 20);
        const auto rises = crossings(r, out, 2.5, +1, 50e-6);
        const auto expected = 2.0 * (10e3 + 50.0) * 10e-9 * std::log(1.5) + 2.0 * 10e-9;
        bool ok = r.ok && rises.size() >= 4;
        double worst = 0.0;
        for (size_t k = 1; ok && k < rises.size(); ++k)
            worst = std::max(worst, std::abs(rises[k] - rises[k - 1] - expected) / expected);
        check(ok && worst < 1e-3, fmt("Schmitt RC oscillator: period %.6g us = 2 (R + rOut) C ln 1.5 + 2 tpd, within 0.1 %%", expected * 1e6),
              r.error + fmt(" worst relative %.3g", worst) + " cycles " + std::to_string(rises.size()));
        if (rises.size() > 2) record.push_back(rises[2] - rises[1]);
        // The capacitor voltage stays between the switching points (plus what it moves in tpd).
        double lo = 5.0, hi = 0.0;
        for (size_t i = 0; i < r.time.size(); ++i)
            if (r.time[i] > 50e-6) { lo = std::min(lo, r.voltages[i][(size_t)in]); hi = std::max(hi, r.voltages[i][(size_t)in]); }
        check(lo > 2.0 - 1e-3 && lo < 2.0 && hi < 3.0 + 1e-3 && hi > 3.0, "the capacitor swings between the 2 V and 3 V switching points",
              fmt("%.6g", lo) + fmt(" .. %.6g", hi));
    }
}

// ---- 5. latches -----------------------------------------------------------------------

void testLatches(std::vector<double>& record)
{
    std::printf("-- sequential: NAND and NOR SR latches, a clocked D latch --\n");
    auto edge = [](double t0, double from, double to) { return std::vector<std::pair<double, double>> { { t0, from }, { t0 + 10e-9, to } }; };
    auto join = [](std::initializer_list<std::vector<std::pair<double, double>>> parts) {
        std::vector<std::pair<double, double>> all { { 0.0, 0.0 } };
        all.clear();
        for (const auto& p : parts) all.insert(all.end(), p.begin(), p.end());
        return all;
    };
    {
        // NAND: Q = NAND(S', Q'), Q' = NAND(R', Q); S' and R' active low.
        Circuit c;
        const auto sn = c.addNode(), rn = c.addNode(), q = c.addNode(), qn = c.addNode();
        auto sWave = join({ { { 0.0, 0.0 } }, edge(1e-6, 0.0, 5.0) });             // set (low) until 1 us
        auto rWave = join({ { { 0.0, 5.0 } }, edge(4e-6, 5.0, 0.0), edge(5e-6, 0.0, 5.0) }); // reset 4..5 us
        c.addVoltageSource("VS", sn, 0, pwl(sWave));
        c.addVoltageSource("VR", rn, 0, pwl(rWave));
        addGate(c, "U1", logic::Function::Nand, { sn, qn }, q);
        addGate(c, "U2", logic::Function::Nand, { rn, q }, qn);
        const auto r = solveTransient(c, 8e-6, 50e-9, {}, 1 << 20);
        struct Probe { double t; bool q; const char* what; };
        const Probe probes[] { { 0.9e-6, true, "set" }, { 3e-6, true, "hold" }, { 4.9e-6, false, "reset" }, { 7e-6, false, "hold" } };
        bool ok = r.ok;
        std::string detail = r.error;
        for (const auto& p : probes)
        {
            const auto vq = at(r, q, p.t), vqn = at(r, qn, p.t);
            record.push_back(vq);
            if (std::abs(vq - (p.q ? 5.0 : 0.0)) > 1e-3 || std::abs(vqn - (p.q ? 0.0 : 5.0)) > 1e-3)
            {
                ok = false;
                detail += std::string(" ") + p.what + fmt(" at %.3g s:", p.t) + fmt(" Q %.4g", vq) + fmt(" Q' %.4g", vqn);
            }
        }
        check(ok, "NAND SR latch: set -> Q 1, holds after S' returns high, reset -> Q 0, holds; Q' always the complement", detail);
    }
    {
        // NOR: Q = NOR(R, Q'), Q' = NOR(S, Q); S and R active high.
        Circuit c;
        const auto s = c.addNode(), rr = c.addNode(), q = c.addNode(), qn = c.addNode();
        auto rWave = join({ { { 0.0, 5.0 } }, edge(1e-6, 5.0, 0.0), edge(5e-6, 0.0, 5.0), edge(6e-6, 5.0, 0.0) });
        auto sWave = join({ { { 0.0, 0.0 } }, edge(2e-6, 0.0, 5.0), edge(3e-6, 5.0, 0.0) });
        c.addVoltageSource("VR", rr, 0, pwl(rWave));
        c.addVoltageSource("VS", s, 0, pwl(sWave));
        addGate(c, "U1", logic::Function::Nor, { rr, qn }, q);
        addGate(c, "U2", logic::Function::Nor, { s, q }, qn);
        const auto r = solveTransient(c, 8e-6, 50e-9, {}, 1 << 20);
        struct Probe { double t; bool q; const char* what; };
        const Probe probes[] { { 0.9e-6, false, "reset" }, { 1.5e-6, false, "hold" }, { 2.9e-6, true, "set" }, { 4e-6, true, "hold" },
                               { 5.9e-6, false, "reset" }, { 7e-6, false, "hold" } };
        bool ok = r.ok;
        std::string detail = r.error;
        for (const auto& p : probes)
        {
            const auto vq = at(r, q, p.t), vqn = at(r, qn, p.t);
            record.push_back(vq);
            if (std::abs(vq - (p.q ? 5.0 : 0.0)) > 1e-3 || std::abs(vqn - (p.q ? 0.0 : 5.0)) > 1e-3)
            {
                ok = false;
                detail += std::string(" ") + p.what + fmt(" at %.3g s:", p.t) + fmt(" Q %.4g", vq) + fmt(" Q' %.4g", vqn);
            }
        }
        check(ok, "NOR SR latch: reset, hold, set, hold, reset, hold; Q' always the complement", detail);
    }
    {
        // Clocked (gated) D latch from four NANDs and an inverter, clock from
        // an ordinary PWL source: Q follows D while CLK is high and holds
        // while it is low.
        Circuit c;
        const auto d = c.addNode(), clk = c.addNode(), dn = c.addNode(), sp = c.addNode(), rp = c.addNode(), q = c.addNode(), qn = c.addNode();
        auto clkWave = join({ { { 0.0, 5.0 } }, edge(0.5e-6, 5.0, 0.0), edge(1e-6, 0.0, 5.0), edge(1.5e-6, 5.0, 0.0),
                              edge(2e-6, 0.0, 5.0), edge(2.5e-6, 5.0, 0.0) });
        auto dWave = join({ { { 0.0, 5.0 } }, edge(0.7e-6, 5.0, 0.0), edge(1.7e-6, 0.0, 5.0), edge(2.7e-6, 5.0, 0.0) });
        c.addVoltageSource("VD", d, 0, pwl(dWave));
        c.addVoltageSource("VCLK", clk, 0, pwl(clkWave));
        addGate(c, "U1", logic::Function::Not, { d }, dn);
        addGate(c, "U2", logic::Function::Nand, { d, clk }, sp);
        addGate(c, "U3", logic::Function::Nand, { dn, clk }, rp);
        addGate(c, "U4", logic::Function::Nand, { sp, qn }, q);
        addGate(c, "U5", logic::Function::Nand, { rp, q }, qn);
        const auto r = solveTransient(c, 3.5e-6, 20e-9, {}, 1 << 20);
        struct Probe { double t; bool q; const char* what; };
        const Probe probes[] { { 0.4e-6, true, "CLK high, D 1" }, { 0.9e-6, true, "CLK low, D fell at 0.7: holds 1" },
                               { 1.4e-6, false, "CLK high, D 0" }, { 1.9e-6, false, "CLK low, D rose at 1.7: holds 0" },
                               { 2.4e-6, true, "CLK high, D 1" }, { 3.2e-6, true, "CLK low, D fell at 2.7: holds 1" } };
        bool ok = r.ok;
        std::string detail = r.error;
        for (const auto& p : probes)
        {
            const auto vq = at(r, q, p.t);
            record.push_back(vq);
            if (std::abs(vq - (p.q ? 5.0 : 0.0)) > 1e-3)
            {
                ok = false;
                detail += std::string(" [") + p.what + fmt("] Q %.4g", vq);
            }
        }
        check(ok, "clocked D latch (4 NAND + NOT, PWL clock): follows D while CLK is high, holds while it is low", detail);
    }
}

// ---- 6. rollback, the stepper's clock, model checks -----------------------------------

void testRollbackAndChecks()
{
    std::printf("-- rollback with gates, located mode, model checks --\n");
    {
        // NAND latch; save at 4.01 us, after R' fell (crossing 4.005 us) and
        // before the output's change at 4.015 us: run on, restore, run again.
        Circuit c;
        const auto sn = c.addNode(), rn = c.addNode(), q = c.addNode(), qn = c.addNode();
        c.addVoltageSource("VS", sn, 0, pwl({ { 0.0, 0.0 }, { 1e-6, 0.0 }, { 1.01e-6, 5.0 } }));
        c.addVoltageSource("VR", rn, 0, pwl({ { 0.0, 5.0 }, { 4e-6, 5.0 }, { 4.01e-6, 0.0 }, { 5e-6, 0.0 }, { 5.01e-6, 5.0 } }));
        auto g1 = gate(logic::Function::Nand, 2), g2 = gate(logic::Function::Nand, 2);
        c.addProgrammable("U1", { sn, qn, q }, g1);
        c.addProgrammable("U2", { rn, q, qn }, g2);
        TransientStepper s(c);
        bool ok = s.init() && s.advance(4.01e-6);
        TransientStepper::State saved;
        ok = ok && s.saveState(saved);
        const bool pending = g2->hasPending() && std::abs(g2->pendingTime() - 4.015e-6) < 1e-15;
        auto run = [&](std::vector<double>& out, std::uint64_t& landings) {
            const auto before = s.stats().deviceEvents;
            for (double t = 4.1e-6; ok && t <= 6e-6 + 1e-12; t += 0.1e-6)
            {
                ok = s.advance(t);
                for (auto v : s.nodeVoltages()) out.push_back(v);
            }
            landings = s.stats().deviceEvents - before;
        };
        std::vector<double> first, second;
        std::uint64_t l1 = 0, l2 = 0;
        run(first, l1);
        ok = ok && s.restoreState(saved) && g2->hasPending();
        run(second, l2);
        check(ok && pending && sameBits(first, second) && l1 == l2 && l1 > 0,
              "save with a gate's change pending (4.015 us), run on, restore, run again: bit for bit, the same event landings",
              s.error() + " landings " + std::to_string(l1) + "/" + std::to_string(l2) + (pending ? "" : " (nothing pending at the save)"));
    }
    {
        // Located mode on a 1 ns clock (the system simulator's participant):
        // tpd 10 ns from a crossing at 12.5 us lands on 12.51 us.
        TransientStepper::Settings located;
        located.locateEvents = true;
        located.timeQuantum = 1e-9;
        Circuit c;
        const auto a = c.addNode(), y = c.addNode();
        c.addVoltageSource("VA", a, 0, rampAt(10e-6, 5e-6));
        logic::Model m;
        m.tr = 0.0;
        addGate(c, "U1", logic::Function::Not, { a }, y, m);
        TransientStepper s(c, {}, located);
        // The input crosses 2.5 V at 12.5 us inside the step to 12.509 us,
        // which schedules the change at 12.51 us: still high there, low after.
        bool ok = s.init() && s.advance(12.509e-6);
        const auto pending = s.nextDeviceEvent();
        const auto before = s.voltage(y);
        ok = ok && s.advance(12.52e-6);
        const auto after = s.voltage(y);
        const double edge = pending;
        check(ok && std::abs(pending - 12.51e-6) < 1e-15 && before > 5.0 - 1e-6 && after < 1e-6,
              "located mode, 1 ns clock: the change is scheduled at 12.51 us; high at 12.509 us, low at 12.52 us",
              s.error() + fmt(" edge %.17g", edge));
    }
    {
        logic::Model m;
        check(logic::problemsWith(m, logic::Function::Nand, 2).empty(), "the default model (5 V CMOS-like) is valid");
        auto zero = m;
        zero.tpd = 0.0;
        check(logic::problemsWith(zero, logic::Function::Nand, 2).find("propagation delay") != std::string::npos,
              "a zero propagation delay is refused (a delay-free loop never settles)");
        check(logic::problemsWith(m, logic::Function::Not, 2).find("not 2") != std::string::npos
                  && logic::problemsWith(m, logic::Function::And, 9).find("not 9") != std::string::npos,
              "input counts outside 1 (NOT, BUFFER) or 2..8 are refused");
        auto bad = m;
        bad.vth = 6.0;
        check(!logic::problemsWith(bad, logic::Function::And, 2).empty(), "a threshold outside the output levels is refused");
        bad = m;
        bad.hysteresis = 6.0;
        check(!logic::problemsWith(bad, logic::Function::And, 2).empty(), "hysteresis pushing a switching point past a rail is refused");
    }
}

int compareDirs(const std::string& a, const std::string& b)
{
    std::printf("-- optimised vs unoptimised (%s vs %s) --\n", a.c_str(), b.c_str());
    for (const char* name : { "dc", "timing", "feedback", "latches" })
    {
        std::ifstream fa(fs::path(a) / (std::string(name) + ".txt")), fb(fs::path(b) / (std::string(name) + ".txt"));
        std::vector<double> va, vb;
        double v;
        while (fa >> v) va.push_back(v);
        while (fb >> v) vb.push_back(v);
        double largest = 0.0;
        bool ok = !va.empty() && va.size() == vb.size();
        for (size_t i = 0; ok && i < va.size(); ++i)
        {
            const auto d = std::abs(va[i] - vb[i]);
            largest = std::max(largest, d);
            ok = d <= 1e-12 + 1e-9 * std::max(std::abs(va[i]), std::abs(vb[i]));
        }
        check(ok, std::string(name) + ": " + std::to_string(va.size()) + " values within 1e-9 relative + 1e-12 absolute",
              std::to_string(va.size()) + " vs " + std::to_string(vb.size()) + " values");
        std::printf("      %s: largest difference %.3g\n", name, largest);
    }
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::string(argv[1]) == "--compare")
        return compareDirs(argv[2], argv[3]);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--out")
            outDir = argv[i + 1];
    fs::create_directories(outDir);
    std::printf("logic gate tests, circuit_sim configuration: %s, output: %s\n", LOGIC_GATE_TESTS_CONFIG, outDir.c_str());
    testTruthTables();
    std::vector<double> dcValues, timing, feedback, latches;
    testDcBehaviour(dcValues);
    testTiming(timing);
    testFeedback(feedback);
    testLatches(latches);
    testRollbackAndChecks();
    writeValues("dc", dcValues);
    writeValues("timing", timing);
    writeValues("feedback", feedback);
    writeValues("latches", latches);
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}

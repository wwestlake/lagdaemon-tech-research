// P1 acceptance tests (architecture revision 3, section 16): the resumable
// TransientStepper in circuit_sim and the electrical participant.
//
// Built twice, against the optimised and the unoptimised circuit_sim and
// sim_core libraries. Each run writes its waveforms and located times to an
// output folder; `--compare A B` checks the two configurations agree within
// the electrical tolerance (1e-9 relative + 1e-12 absolute per sample).
//
// Usage: circuit_stepper_tests [--out <dir>]
//        circuit_stepper_tests --compare <dirA> <dirB>

#include "../../Source/CircuitSolver.h"
#include "../../Source/ElectricalParticipant.h"

#include "sim_core/Simulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef CIRCUIT_STEPPER_TESTS_CONFIG
#define CIRCUIT_STEPPER_TESTS_CONFIG "unknown"
#endif

using namespace circuit_sim;
namespace fs = std::filesystem;

namespace
{
int failures = 0, passes = 0;
std::string outDir = "circuit_stepper_test_out";
const bool optimisedBuild = std::string(CIRCUIT_STEPPER_TESTS_CONFIG) == "optimized";

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

bool sameBits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

bool sameBits(const std::vector<double>& a, const std::vector<double>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

// One value per line, %.17g, for the cross-configuration comparison.
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

// 0 -> `level` from t = 0 over `rise` seconds, then held. The default 1 us
// rise is a corner the steps land on; a much shorter one is merged away (as
// solveTransient merges corners closer than 1e-3 of a step).
Waveform stepUp(double level, double rise = 1e-6)
{
    Waveform w;
    w.kind = Waveform::Kind::Pulse;
    w.offset = 0.0;
    w.pulsed = level;
    w.delay = 0.0;
    w.rise = rise;
    w.width = 1e3;
    w.fall = 1e-9;
    return w;
}

// RC (or any first-order) response to a ramp from 0 to 1 over T, then held:
// for t >= T, v = 1 - (tau/T)(e^{T/tau} - 1) e^{-t/tau}.
double rampedStep(double t, double tau, double T)
{
    if (t <= T)
        return (t - tau * (1.0 - std::exp(-t / tau))) / T;
    return 1.0 - (tau / T) * std::expm1(T / tau) * std::exp(-t / tau);
}

// ---- a programmable device with state ---------------------------------------

// One pin; integrates its pin voltage (trapezoid over accepted steps) into s
// and draws i = k s from the pin: a state that changes the circuit, the kind
// of device whose committed state rollback must restore.
class Integrator final : public ProgrammableDevice
{
public:
    explicit Integrator(double gain, bool snapshots = true) : k(gain), canSnapshot(snapshots) {}
    void reset() override { committedS = trialS = committedV = trialV = 0.0; }
    bool evaluate(double, double h, const std::vector<double>& v, std::vector<double>& currents, std::string&) override
    {
        trialV = v[0];
        trialS = committedS + 0.5 * h * (committedV + v[0]);
        currents = { -k * trialS };
        return true;
    }
    void accept(double) override
    {
        committedS = trialS;
        committedV = trialV;
    }
    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        if (!canSnapshot) return false;
        const double values[] { committedS, trialS, committedV, trialV };
        out.resize(sizeof values);
        std::memcpy(out.data(), values, sizeof values);
        return true;
    }
    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (!canSnapshot || in.size() != 4 * sizeof(double)) return false;
        double values[4];
        std::memcpy(values, in.data(), sizeof values);
        committedS = values[0]; trialS = values[1]; committedV = values[2]; trialV = values[3];
        return true;
    }
    double state() const { return committedS; }

private:
    double k;
    bool canSnapshot;
    double committedS = 0.0, trialS = 0.0, committedV = 0.0, trialV = 0.0;
};

// ---- 1. the stepper reproduces solveTransient() bit for bit -------------------

struct Case
{
    std::string name;
    Circuit circuit;
    double stop = 1e-3, step = 1e-6;
};

std::vector<Case> batchCases()
{
    std::vector<Case> cases;
    {
        Case k { "RC with a pulse train" };
        auto& c = k.circuit;
        const auto in = c.addNode(), out = c.addNode();
        Waveform p;
        p.kind = Waveform::Kind::Pulse;
        p.offset = 0.0; p.pulsed = 1.0; p.delay = 10e-6; p.rise = 1e-6; p.width = 200e-6; p.fall = 1e-6; p.period = 500e-6;
        c.addVoltageSource("V1", in, 0, p);
        c.addResistor("R1", in, out, 1e3);
        c.addCapacitor("C1", out, 0, 100e-9);
        k.stop = 2e-3; k.step = 5e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "series RLC from a step" };
        auto& c = k.circuit;
        const auto a = c.addNode(), b = c.addNode(), d = c.addNode();
        c.addVoltageSource("V1", a, 0, stepUp(5.0));
        c.addResistor("R1", a, b, 10.0);
        c.addInductor("L1", b, d, 1e-3);
        c.addCapacitor("C1", d, 0, 1e-6);
        k.stop = 400e-6; k.step = 0.5e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "diode rectifier (nonlinear)" };
        auto& c = k.circuit;
        const auto a = c.addNode(), b = c.addNode();
        Waveform s;
        s.kind = Waveform::Kind::Sine;
        s.amplitude = 5.0; s.frequency = 1e3;
        c.addVoltageSource("V1", a, 0, s);
        c.addDiode("D1", a, b);
        c.addResistor("R1", b, 0, 1e3);
        c.addCapacitor("C1", b, 0, 10e-6);
        k.stop = 3e-3; k.step = 10e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "BJT common-emitter amplifier" };
        auto& c = k.circuit;
        const auto vcc = c.addNode(), in = c.addNode(), base = c.addNode(), col = c.addNode(), em = c.addNode();
        c.addVoltageSource("VCC", vcc, 0, dc(12.0));
        Waveform s;
        s.kind = Waveform::Kind::Sine;
        s.amplitude = 0.05; s.frequency = 2e3;
        c.addVoltageSource("VIN", in, 0, s);
        c.addCapacitor("CIN", in, base, 10e-6);
        c.addResistor("RB1", vcc, base, 47e3);
        c.addResistor("RB2", base, 0, 10e3);
        c.addResistor("RC", vcc, col, 4.7e3);
        c.addResistor("RE", em, 0, 1e3);
        c.addBjt("Q1", true, col, base, em);
        k.stop = 2e-3; k.step = 5e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "voltage-controlled switch (batch rule)" };
        auto& c = k.circuit;
        const auto ctl = c.addNode(), sup = c.addNode(), out = c.addNode();
        Waveform s;
        s.kind = Waveform::Kind::Sine;
        s.amplitude = 1.0; s.frequency = 1e3;
        c.addVoltageSource("VC", ctl, 0, s);
        c.addVoltageSource("VS", sup, 0, dc(5.0));
        c.addVoltageControlledSwitch("S1", sup, out, ctl, 0, 1.0, 1e9, 0.2, 0.0);
        c.addResistor("RL", out, 0, 1e3);
        c.addCapacitor("CL", out, 0, 1e-6);
        k.stop = 3e-3; k.step = 10e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "coupled inductors (transformer)" };
        auto& c = k.circuit;
        const auto a = c.addNode(), p = c.addNode(), s = c.addNode();
        Waveform w;
        w.kind = Waveform::Kind::Sine;
        w.amplitude = 10.0; w.frequency = 5e3;
        c.addVoltageSource("V1", a, 0, w);
        c.addResistor("RP", a, p, 10.0);
        const auto lp = c.addInductor("LP", p, 0, 1e-3);
        const auto ls = c.addInductor("LS", s, 0, 4e-3);
        c.addCoupling("K1", lp, ls, 0.95);
        c.addResistor("RL", s, 0, 100.0);
        k.stop = 1e-3; k.step = 1e-6;
        cases.push_back(std::move(k));
    }
    {
        Case k { "programmable device with state" };
        auto& c = k.circuit;
        const auto a = c.addNode(), b = c.addNode();
        Waveform s;
        s.kind = Waveform::Kind::Sine;
        s.amplitude = 1.0; s.frequency = 500.0;
        c.addVoltageSource("V1", a, 0, s);
        c.addResistor("R1", a, b, 1e3);
        c.addProgrammable("U1", { b }, std::make_shared<Integrator>(1.0));
        k.stop = 4e-3; k.step = 10e-6;
        cases.push_back(std::move(k));
    }
    return cases;
}

void testStepperMatchesBatch()
{
    std::printf("-- the stepper reproduces solveTransient() --\n");
    for (auto& k : batchCases())
    {
        TransientSettings s;
        s.stop = k.stop;
        s.step = k.step;
        s.maxSamples = 1 << 30; // keep every sample
        const auto batch = solveTransient(k.circuit, s);
        TransientStepper stepper(k.circuit);
        const auto grid = TransientStepper::timeGrid(k.circuit, k.stop, k.step);
        bool ok = batch.ok && stepper.init() && batch.time.size() == grid.size() + 1;
        size_t mismatch = 0;
        ok = ok && sameBits(batch.voltages[0], stepper.nodeVoltages()) && sameBits(batch.sourceCurrents[0], stepper.sourceCurrents());
        for (size_t n = 0; ok && n < grid.size(); ++n)
        {
            ok = stepper.step(grid[n]) && sameBits(batch.time[n + 1], grid[n]) && sameBits(batch.voltages[n + 1], stepper.nodeVoltages())
              && sameBits(batch.sourceCurrents[n + 1], stepper.sourceCurrents());
            if (!ok) mismatch = n + 1;
        }
        check(ok, k.name + ": " + std::to_string(grid.size()) + " steps, every voltage and current bit for bit",
              batch.error + stepper.error() + " first difference at sample " + std::to_string(mismatch));
    }
}

// ---- 2. analytic references ---------------------------------------------------

void testRcAndRlc()
{
    std::printf("-- analytic references --\n");
    const double R = 1e3, C = 1e-6, tau = R * C, T = 1e-6;
    {
        Circuit c;
        const auto in = c.addNode(), out = c.addNode();
        c.addVoltageSource("V1", in, 0, stepUp(1.0, T));
        c.addResistor("R1", in, out, R);
        c.addCapacitor("C1", out, 0, C);
        TransientStepper stepper(c);
        bool ok = stepper.init();
        double worst = 0.0;
        std::vector<double> wave;
        for (int n = 1; ok && n <= 500; ++n)
        {
            ok = stepper.advance(n * 10e-6);
            const auto t = stepper.time();
            worst = std::max(worst, std::abs(stepper.voltage(out) - rampedStep(t, tau, T)));
            wave.push_back(stepper.voltage(out));
        }
        check(ok && worst <= 1e-3 * 1.0 + 1e-6, "RC step (tau 1 ms, 10 us steps, 5 tau): within 0.1 % of the analytic response",
              "worst " + fmt("%.3g V", worst));
        std::printf("      RC worst error %.3g V (limit 1.0e-03 V)\n", worst);
        writeValues("rc_step", wave);

        // Unequal macro steps (T1: the interface never assumes equal steps).
        TransientStepper uneven(c);
        ok = uneven.init();
        double worstUneven = 0.0, t = 0.0;
        const double pattern[] { 3e-6, 17e-6, 9e-6, 25e-6, 1e-6, 11e-6 };
        for (int n = 0; ok && t < 5e-3; ++n)
        {
            t = std::min(5e-3, t + pattern[n % 6]);
            ok = uneven.advance(t);
            worstUneven = std::max(worstUneven, std::abs(uneven.voltage(out) - rampedStep(t, tau, T)));
        }
        check(ok && worstUneven <= 1e-3 + 1e-6, "RC step with unequal steps (1 to 25 us): within 0.1 %", fmt("worst %.3g V", worstUneven));
    }
    {
        // Series RLC driven by a 5 V step: i(t) = V/(wd L) e^{-a t} sin(wd t).
        const double L = 1e-3, Cr = 1e-6, Rr = 10.0, V = 5.0, Tr = 1e-9;
        Circuit c;
        const auto a = c.addNode(), b = c.addNode(), d = c.addNode();
        c.addVoltageSource("V1", a, 0, stepUp(V, Tr));
        c.addResistor("R1", a, b, Rr);
        const auto l = c.addInductor("L1", b, d, L);
        c.addCapacitor("C1", d, 0, Cr);
        const double alpha = Rr / (2 * L), w0 = 1.0 / std::sqrt(L * Cr), wd = std::sqrt(w0 * w0 - alpha * alpha);
        const double peak = V / (wd * L);
        TransientStepper stepper(c);
        bool ok = stepper.init();
        double worst = 0.0;
        std::vector<double> wave;
        for (int n = 1; ok && n <= 800; ++n)
        {
            ok = stepper.advance(n * 0.5e-6);
            const auto t = stepper.time() - Tr / 2; // the 1 ns ramp delays the step by half its length
            const auto ref = peak * std::exp(-alpha * t) * std::sin(wd * t);
            worst = std::max(worst, std::abs(stepper.branchCurrent(l) - ref));
            wave.push_back(stepper.branchCurrent(l));
        }
        check(ok && worst <= 1e-3 * peak + 1e-9, "RLC ring-down (two periods, 0.5 us steps): within 0.1 % of the peak current",
              fmt("worst %.3g A", worst) + fmt(" of peak %.4g A", peak));
        std::printf("      RLC worst error %.3g A, peak %.4g A (limit %.3g A)\n", worst, peak, 1e-3 * peak);
        writeValues("rlc_ring", wave);
    }
}

// ---- 3. event location -----------------------------------------------------------

void testComparatorLocation()
{
    std::printf("-- event location --\n");
    const double R = 1e3, C = 1e-6, tau = R * C, T = 1e-6;
    Circuit c;
    const auto in = c.addNode(), cap = c.addNode(), aux = c.addNode(), ind = c.addNode();
    c.addVoltageSource("V1", in, 0, stepUp(1.0, T));
    c.addResistor("R1", in, cap, R);
    c.addCapacitor("C1", cap, 0, C);
    c.addVoltageSource("VA", aux, 0, dc(1.0));
    const auto sw = c.addVoltageControlledSwitch("S1", aux, ind, cap, 0, 1.0, 1e9, 0.5, 0.0);
    c.addResistor("RI", ind, 0, 1e3);
    // Exact crossing times of v = 1 - K e^{-t/tau}: K e^{-t/tau} = 1 - level.
    const double K = (tau / T) * std::expm1(T / tau);
    const double tSwitch = tau * std::log(K / 0.5), tWatch = tau * std::log(K / 0.75);

    TransientStepper::Settings settings;
    settings.locateEvents = true;
    TransientStepper stepper(c, {}, settings);
    TransientStepper::Watch w;
    w.plus = cap;
    w.threshold = 0.25;
    w.direction = 1;
    const auto watch = stepper.addWatch(w);
    bool ok = stepper.init();
    check(ok && !stepper.switchClosed(sw), "the comparator switch starts open (0 V < 0.5 V threshold)", stepper.error());
    std::vector<TransientStepper::Event> events;
    const double step = 10e-6;
    double target = step;
    int stops = 0;
    while (ok && stepper.time() < 1e-3)
    {
        const auto before = events.size();
        ok = stepper.advance(target, &events);
        if (stepper.time() < target) ++stops;
        if (stepper.time() >= target) target += step;
        (void)before;
    }
    double switchAt = -1.0, watchAt = -1.0;
    int switchEvents = 0, watchEvents = 0;
    for (const auto& e : events)
    {
        if (e.element == sw) { switchAt = e.time; ++switchEvents; }
        if (e.watch == watch) { watchAt = e.time; ++watchEvents; }
    }
    check(ok && switchEvents == 1 && std::abs(switchAt - tSwitch) <= 10e-9,
          "comparator (switch at 0.5 V) located within 10 ns of the analytic crossing, with 10 us steps",
          fmt("located %.12g s", switchAt) + fmt(" analytic %.12g s", tSwitch) + " events " + std::to_string(switchEvents));
    check(ok && watchEvents == 1 && std::abs(watchAt - tWatch) <= 10e-9, "a watched threshold (0.25 V rising) located within 10 ns",
          fmt("located %.12g s", watchAt) + fmt(" analytic %.12g s", tWatch));
    std::printf("      switch: located %.12f ms, analytic %.12f ms, error %+.3g ns\n", switchAt * 1e3, tSwitch * 1e3, (switchAt - tSwitch) * 1e9);
    std::printf("      watch:  located %.12f ms, analytic %.12f ms, error %+.3g ns\n", watchAt * 1e3, tWatch * 1e3, (watchAt - tWatch) * 1e9);
    std::printf("      %llu location steps for %llu events\n", (unsigned long long)stepper.stats().locationSteps,
                (unsigned long long)stepper.stats().events);
    check(ok && stepper.switchClosed(sw) && std::abs(stepper.voltage(ind) - 1.0 * 1e3 / (1e3 + 1.0)) < 1e-9,
          "after the crossing the switch is closed and the indicator reads 0.999 V", fmt("%.9g V", stepper.voltage(ind)));
    check(stops == 2, "advance() stopped at each located event (2 stops)", std::to_string(stops));
    writeValues("comparator_times", { switchAt, watchAt });
}

// ---- 4. restart after a located switch opening ------------------------------------

// A current interruption, the classic case for trapezoidal ringing: 10 V -
// R1 100 ohm - L 0.5 mH - a switch to ground (Ron 1 mohm, Roff 1 Gohm, the
// Workbench's switch model), as a relay coil switched by a contact with no
// flyback path. The switch opens when its control ramps through 0 V at 1 ms;
// 20 us steps.
const double rlVs = 10.0, rlR1 = 100.0, rlL = 0.5e-3, rlRoff = 1e9;

struct RlRun
{
    bool ok = false;
    double eventTime = 0.0, i0 = 0.0;
    std::vector<double> times, vl;
    std::string error;
};

RlRun runSwitchedRl(bool restart)
{
    Circuit c;
    const auto n1 = c.addNode(), n2 = c.addNode(), n3 = c.addNode(), ctl = c.addNode();
    c.addVoltageSource("VS", n1, 0, dc(rlVs));
    c.addResistor("R1", n1, n2, rlR1);
    const auto l = c.addInductor("L1", n2, n3, rlL);
    Waveform ramp;
    ramp.kind = Waveform::Kind::Pwl;
    ramp.points = { { 0.0, 1.0 }, { 0.5e-3, 1.0 }, { 1.5e-3, -1.0 } };
    c.addVoltageSource("VC", ctl, 0, ramp);
    const auto sw = c.addVoltageControlledSwitch("S1", n3, 0, ctl, 0, 1e-3, rlRoff, 0.0, 0.0);
    TransientStepper::Settings settings;
    settings.locateEvents = true;
    settings.timeQuantum = 1e-12;
    settings.restartAfterDiscontinuity = restart;
    TransientStepper stepper(c, {}, settings);
    RlRun run;
    bool ok = stepper.init();
    std::vector<TransientStepper::Event> events;
    double target = 20e-6;
    while (ok && stepper.time() < 1.5e-3)
    {
        const auto before = events.size();
        const auto iBefore = stepper.branchCurrent(l);
        ok = stepper.advance(target, &events);
        if (ok && events.size() > before && events.back().element == sw)
        {
            run.eventTime = events.back().time;
            run.i0 = iBefore; // the inductor current is continuous through the opening
        }
        else if (ok && run.eventTime > 0.0)
        {
            run.times.push_back(stepper.time());
            run.vl.push_back(stepper.voltage(n2) - stepper.voltage(n3));
        }
        if (stepper.time() >= target) target += 20e-6;
    }
    run.ok = ok && run.eventTime > 0.0;
    run.error = stepper.error();
    return run;
}

void testRestartRemovesRinging()
{
    std::printf("-- restart after a located switch opening --\n");
    // Analytic: the current falls from I0 to Vs / (R1 + Roff) with time
    // constant L / (R1 + Roff) = 0.5 ps, so the inductor voltage
    // v_L = -(R1 + Roff)(i - i_ss) is a negative spike that has decayed to
    // nothing (staying negative) long before the first step ends. Any
    // positive sample beyond the electrical tolerance's 1 uV floor is a
    // reversal the analytic response does not have.
    const double floor = 1e-6;
    const auto with = runSwitchedRl(true);
    const auto without = runSwitchedRl(false);
    auto reversals = [&](const RlRun& r, double& largest) {
        int count = 0;
        largest = 0.0;
        for (auto v : r.vl)
        {
            if (v > floor) ++count;
            largest = std::max(largest, std::abs(v));
        }
        return count;
    };
    double largestWith = 0.0, largestWithout = 0.0;
    const auto nWith = reversals(with, largestWith), nWithout = reversals(without, largestWithout);
    double residual = 0.0; // largest |v_L| after the restart step
    for (size_t k = 1; k < with.vl.size(); ++k)
        residual = std::max(residual, std::abs(with.vl[k]));
    check(with.ok && without.ok && std::abs(with.eventTime - 1e-3) <= 10e-9,
          "the switch opening is located at 1 ms (control ramp through 0 V)", fmt("%.12g s ", with.eventTime) + with.error);
    check(without.ok && nWithout > 10, "control: trapezoidal steps alone ring after the interruption (the inductor voltage alternates in sign)",
          std::to_string(nWithout) + fmt(" positive samples, amplitude %.3g V", largestWithout));
    check(with.ok && nWith == 0 && !with.vl.empty() && with.vl[0] < 0.0,
          "with the restart: no sign reversal in the inductor voltage beyond the 1 uV floor, over 25 steps",
          std::to_string(nWith) + fmt(" positive samples, largest |v_L| %.3g V", residual));
    std::printf("      I0 = %.6f A; without restart the inductor voltage rings at +/- %.4g V (trapezoidal: 2 L I0 / h = %.4g V); "
                "with it, the restart step reads %.4g V and every later sample is within %.3g V of zero\n",
                with.i0, largestWithout, 2 * rlL * with.i0 / 20e-6, with.vl.empty() ? 0.0 : with.vl[0], residual);
    std::vector<double> dump = with.vl;
    dump.push_back(with.eventTime);
    writeValues("rl_restart", dump);
}

// ---- 5. save, restore, re-step; rollback --------------------------------------

struct Rig
{
    Circuit circuit;
    std::shared_ptr<Integrator> device;
    Node out = 0, cap = 0;
    int sw = -1;
};

// The comparator RC plus an integrating programmable device on the switched
// output and an inductor: every kind of state the stepper keeps.
Rig makeRig()
{
    Rig r;
    auto& c = r.circuit;
    const auto in = c.addNode(), cap = c.addNode(), aux = c.addNode(), out = c.addNode(), mid = c.addNode();
    c.addVoltageSource("V1", in, 0, stepUp(1.0));
    c.addResistor("R1", in, cap, 1e3);
    c.addCapacitor("C1", cap, 0, 1e-6);
    c.addVoltageSource("VA", aux, 0, dc(2.0));
    r.sw = c.addVoltageControlledSwitch("S1", aux, out, cap, 0, 1.0, 1e9, 0.5, 0.0);
    c.addResistor("RO", out, mid, 100.0);
    c.addInductor("LO", mid, 0, 1e-3);
    r.device = std::make_shared<Integrator>(1e-1);
    c.addProgrammable("U1", { out }, r.device);
    r.out = out;
    r.cap = cap;
    return r;
}

std::vector<double> runFrom(TransientStepper& s, double until, double step)
{
    std::vector<double> wave;
    double target = (std::floor(s.time() / step + 1e-9) + 1.0) * step;
    while (s.time() < until - 1e-15)
    {
        if (!s.advance(std::min(target, until)))
            return { -1.0 };
        auto v = s.nodeVoltages();
        wave.push_back(s.time());
        wave.insert(wave.end(), v.begin(), v.end());
        if (s.time() >= target) target += step;
    }
    return wave;
}

void testSaveRestore()
{
    std::printf("-- save, restore, re-step --\n");
    TransientStepper::Settings located;
    located.locateEvents = true;
    auto rig = makeRig();
    TransientStepper s(rig.circuit, {}, located);
    bool ok = s.init();
    runFrom(s, 0.5e-3, 20e-6);
    TransientStepper::State saved;
    ok = ok && s.saveState(saved);
    const auto deviceAtSave = rig.device->state();
    const auto first = runFrom(s, 1.2e-3, 20e-6);     // through the switch closing near 0.69 ms
    const bool closedAfter = s.switchClosed(rig.sw);
    ok = ok && s.restoreState(saved);
    const bool deviceBack = rig.device->state() == deviceAtSave;
    const auto second = runFrom(s, 1.2e-3, 20e-6);
    check(ok && closedAfter && sameBits(first, second) && first.size() > 10,
          "save at 0.5 ms, run through the located switch closing, restore, re-run: identical waveform bit for bit",
          s.error() + " samples " + std::to_string(first.size()));
    check(deviceBack, "restoring puts the programmable device's committed state back");

    // The saved state is complete: a second stepper on its own copy of the
    // circuit (its own device) continues identically from it.
    auto rig2 = makeRig();
    TransientStepper other(rig2.circuit, {}, located);
    ok = other.init() && other.restoreState(saved);
    const auto third = runFrom(other, 1.2e-3, 20e-6);
    check(ok && sameBits(first, third), "a fresh stepper restored from the saved state continues bit for bit the same", other.error());

    // A device that cannot save state: the stepper says so rather than
    // rolling back half a circuit.
    Circuit plain;
    const auto a = plain.addNode();
    plain.addVoltageSource("V1", a, 0, dc(1.0));
    plain.addProgrammable("U9", { a }, std::make_shared<Integrator>(1.0, false));
    TransientStepper noSnap(plain);
    TransientStepper::State st;
    const bool initOk = noSnap.init();
    check(initOk && !noSnap.saveState(st) && noSnap.error().find("U9") != std::string::npos,
          "a programmable device without snapshots: saveState() refuses, naming it", noSnap.error());
    TransientStepper noSnapLocated(plain, {}, located);
    check(!noSnapLocated.init() && noSnapLocated.error().find("U9") != std::string::npos,
          "and event location (which re-steps from saved states) is refused for it", noSnapLocated.error());
    writeValues("save_restore", first);
}

void testRollback()
{
    std::printf("-- rollback: a rejected trial leaves no trace --\n");
    TransientStepper::Settings located;
    located.locateEvents = true;
    auto rig = makeRig();
    TransientStepper s(rig.circuit, {}, located);
    bool ok = s.init();
    runFrom(s, 0.66e-3, 20e-6);
    TransientStepper::State committed;
    ok = ok && s.saveState(committed);
    // A trial past the switch closing (~0.693 ms): the switch flips, a restart
    // is armed and the device integrates on.
    std::vector<TransientStepper::Event> trialEvents;
    ok = ok && s.advance(0.75e-3, &trialEvents);
    const bool trialSwitched = !trialEvents.empty() && s.switchClosed(rig.sw);
    // Rejected: back to the committed state, then a shorter step before the crossing.
    ok = ok && s.restoreState(committed);
    ok = ok && s.advance(0.68e-3);
    const auto afterRollback = s.nodeVoltages();
    const auto deviceAfter = rig.device->state();
    const auto stillOpen = !s.switchClosed(rig.sw);
    const auto restarts = s.stats().restarts;

    // Reference: the same committed state, the short step only.
    auto rig2 = makeRig();
    TransientStepper ref(rig2.circuit, {}, located);
    ok = ok && ref.init() && ref.restoreState(committed) && ref.advance(0.68e-3);
    check(ok && trialSwitched, "the trial to 0.75 ms located and applied the switch closing", s.error());
    check(ok && stillOpen && sameBits(afterRollback, ref.nodeVoltages()) && deviceAfter == rig2.device->state(),
          "after rollback and a shorter step: switch open, voltages and device state identical to never having tried");
    check(ok && restarts == ref.stats().restarts, "the rejected trial's restart step was not carried into the accepted one",
          std::to_string(restarts) + " vs " + std::to_string(ref.stats().restarts));
}

// ---- 6. the electrical participant in the scheduler ---------------------------

sim::Tick us(double v) { return (sim::Tick)std::llround(v * 1e6); }

// Logs the ticks of events on its input.
struct EventLog final : sim::Participant
{
    std::shared_ptr<std::vector<sim::Tick>> ticks = std::make_shared<std::vector<sim::Tick>>();
    sim::ParticipantInfo describe() const override
    {
        sim::ParticipantInfo i;
        i.name = "log";
        auto p = sim::PortSpec::event("in");
        p.required = false;
        i.inputs = { p };
        i.timing = { sim::TimingKind::FixedStep, 1'000'000'000'000LL, 1 };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    sim::Status initialize(sim::Tick, int, const sim::InputFrame&) override { return {}; }
    sim::StepResult doStep(sim::Tick, sim::Tick, const sim::InputFrame&, sim::EventSink&) override { return {}; }
    void getOutputs(sim::OutputFrame&) const override {}
    sim::Status handleEvent(const sim::Event& e, int, const sim::InputFrame&, sim::EventSink&) override
    {
        ticks->push_back(e.time.tick);
        return {};
    }
    std::vector<std::uint8_t> saveState() const override { return { 0 }; }
    sim::Status restoreState(const std::vector<std::uint8_t>&) override { return {}; }
};

// A ramp generator, v = rate * t, at fixed 100 us steps, supplying its rate
// so the electrical participant reads the ramp exactly between its steps.
struct Ramp final : sim::Participant
{
    double rate = 1000.0;
    sim::Tick t = 0, trial = 0;
    sim::ParticipantInfo describe() const override
    {
        sim::ParticipantInfo i;
        i.name = "gen";
        i.outputs = { sim::PortSpec::continuous("v", "V", 1) };
        i.timing = { sim::TimingKind::FixedStep, 100'000'000, 1 };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    sim::Status initialize(sim::Tick t0, int, const sim::InputFrame&) override { t = trial = t0; return {}; }
    sim::StepResult doStep(sim::Tick, sim::Tick to, const sim::InputFrame&, sim::EventSink&) override { trial = to; return {}; }
    void commit() override { t = trial; }
    void rollback() override { trial = t; }
    void getOutputs(sim::OutputFrame& out) const override { out.setReal(0, rate * sim::ticksToSeconds(trial), rate); }
    std::vector<std::uint8_t> saveState() const override
    {
        std::vector<std::uint8_t> b(sizeof t);
        std::memcpy(b.data(), &t, sizeof t);
        return b;
    }
    sim::Status restoreState(const std::vector<std::uint8_t>& b) override
    {
        std::memcpy(&t, b.data(), sizeof t);
        trial = t;
        return {};
    }
};

// Stops early at a scripted tick: forces the scheduler to roll back and
// re-step every locator that already stepped past it.
struct EarlyStopper final : sim::Participant
{
    sim::Tick stopAt = 0;
    sim::Tick t = 0, trial = 0;
    bool done = false, trialDone = false;
    sim::ParticipantInfo describe() const override
    {
        sim::ParticipantInfo i;
        i.name = "stopper";
        // The electrical participant's step: a locator with a longer step
        // would lag the clock, and the P0 scheduler cannot yet handle an event
        // located behind it (reported as a P0 limitation).
        i.timing = { sim::TimingKind::VariableStep, 10'000'000, 1 };
        i.caps.canRollback = true;
        i.caps.locatesEvents = true;
        i.caps.stateSerializable = true;
        return i;
    }
    sim::Status initialize(sim::Tick t0, int, const sim::InputFrame&) override { t = trial = t0; return {}; }
    sim::StepResult doStep(sim::Tick, sim::Tick to, const sim::InputFrame&, sim::EventSink&) override
    {
        sim::StepResult r;
        trial = to;
        trialDone = done;
        if (!done && stopAt < to)
        {
            r.stoppedEarly = true;
            r.stoppedAt = stopAt;
            trial = stopAt;
            trialDone = true;
        }
        return r;
    }
    void commit() override { t = trial; done = trialDone; }
    void rollback() override { trial = t; trialDone = done; }
    void getOutputs(sim::OutputFrame&) const override {}
    std::vector<std::uint8_t> saveState() const override { return { (std::uint8_t)(done ? 1 : 0) }; }
    sim::Status restoreState(const std::vector<std::uint8_t>& b) override { done = !b.empty() && b[0] != 0; return {}; }
};

// Captures every committed round.
struct Capture final : sim::RecordingSink
{
    std::vector<sim::RoundRecord> rounds;
    void begin(const sim::RunDescription&) override {}
    void round(sim::RoundRecord&& r) override { rounds.push_back(std::move(r)); }
    void end() override {}
};

struct ComparatorCircuit
{
    Circuit circuit;
    Node cap = 0;
    int sw = -1, source = -1;
};

ComparatorCircuit comparatorCircuit(bool sourceFromBus)
{
    ComparatorCircuit k;
    auto& c = k.circuit;
    const auto in = c.addNode(), cap = c.addNode(), aux = c.addNode(), ind = c.addNode();
    k.source = c.addVoltageSource("V1", in, 0, sourceFromBus ? dc(0.0) : stepUp(1.0));
    c.addResistor("R1", in, cap, 1e3);
    c.addCapacitor("C1", cap, 0, 1e-6);
    c.addVoltageSource("VA", aux, 0, dc(1.0));
    k.sw = c.addVoltageControlledSwitch("S1", aux, ind, cap, 0, 1.0, 1e9, 0.5, 0.0);
    c.addResistor("RI", ind, 0, 1e3);
    k.cap = cap;
    return k;
}

electrical::Config comparatorConfig(const ComparatorCircuit& k, bool sourceInput)
{
    electrical::Config cfg;
    cfg.name = "el";
    cfg.step = us(10);
    cfg.outputs.push_back({ electrical::OutputBinding::Kind::NodeVoltage, "vc", k.cap, 0, -1 });
    cfg.outputs.push_back({ electrical::OutputBinding::Kind::SwitchState, "s1", 0, 0, k.sw });
    electrical::EventBinding e;
    e.port = "crossed";
    e.watch.plus = k.cap;
    e.watch.threshold = 0.5;
    e.watch.direction = 1;
    cfg.events.push_back(e);
    if (sourceInput)
        cfg.inputs.push_back({ electrical::InputBinding::Kind::SourceValue, "vin", k.source });
    return cfg;
}

void testParticipant()
{
    std::printf("-- the electrical participant --\n");
    const double tau = 1e-3, T = 1e-6;
    const double tCross = tau * std::log((tau / T) * std::expm1(T / tau) / 0.5);
    {
        auto k = comparatorCircuit(false);
        sim::Simulation simulation;
        auto log = std::make_unique<EventLog>();
        auto ticks = log->ticks;
        simulation.addParticipant(std::make_unique<electrical::Participant>(k.circuit, comparatorConfig(k, false)));
        simulation.addParticipant(std::move(log));
        auto status = simulation.connect("el.crossed", "log.in");
        if (status) status = simulation.configure();
        if (status) status = simulation.initialize(0);
        if (status) status = simulation.start();
        if (status) status = simulation.runUntil(us(1000));
        const bool one = ticks->size() == 1;
        const double at = one ? sim::ticksToSeconds((*ticks)[0]) : -1.0;
        check(status.ok && one && std::abs(at - tCross) <= 10e-9,
              "a located crossing becomes an event at a whole tick within 10 ns of the analytic time",
              status.message + fmt(" event at %.12g s", at) + fmt(" analytic %.12g s", tCross));
        const auto s1 = simulation.committedValue(simulation.signalId("el.s1"));
        check(s1.asBoolean(), "the comparator switch state output reads closed after the crossing");
        std::printf("      event at tick %lld (%.12f ms), analytic %.12f ms\n", one ? (long long)(*ticks)[0] : -1LL, at * 1e3, tCross * 1e3);
    }
    {
        // A live input: the ramp generator drives the RC's source through the
        // bus; v_c follows the analytic ramp response.
        (void)T;
        auto k = comparatorCircuit(true);
        sim::Simulation simulation;
        simulation.addParticipant(std::make_unique<Ramp>());
        auto cfg = comparatorConfig(k, true);
        cfg.events.clear();
        simulation.addParticipant(std::make_unique<electrical::Participant>(k.circuit, cfg));
        auto status = simulation.connect("gen.v", "el.vin");
        if (status) status = simulation.configure();
        if (status) status = simulation.initialize(0);
        if (status) status = simulation.start();
        double worst = 0.0;
        const int vc = simulation.signalId("el.vc");
        for (int n = 1; status && n <= 50; ++n)
        {
            status = simulation.runUntil(us(20.0 * n));
            const double t = sim::ticksToSeconds(simulation.now().tick);
            const double ref = 1000.0 * (t - tau * (1.0 - std::exp(-t / tau)));
            worst = std::max(worst, std::abs(simulation.committedValue(vc).real - ref));
        }
        check(status.ok && worst <= 1e-3 * 1.0 + 1e-6, "a source driven live from another participant (ramp 1 V/ms): within 0.1 % of analytic",
              status.message + fmt(" worst %.3g V", worst));
    }
    {
        // Rollback through the scheduler: a locator that stops early inside
        // the electrical participant's step makes the scheduler roll it back
        // and re-step it. Re-stepping a fresh participant directly through the
        // committed round times gives the same values bit for bit, so the
        // rejected trials left nothing behind.
        auto k = comparatorCircuit(false);
        sim::Simulation simulation;
        Capture capture;
        simulation.setRecorder(&capture);
        auto el = std::make_unique<electrical::Participant>(k.circuit, comparatorConfig(k, false));
        auto* elp = el.get();
        simulation.addParticipant(std::move(el));
        auto stopper = std::make_unique<EarlyStopper>();
        stopper->stopAt = us(105.5);
        simulation.addParticipant(std::move(stopper));
        auto status = simulation.configure();
        if (status) status = simulation.initialize(0);
        if (status) status = simulation.start();
        if (status) status = simulation.runUntil(us(1000));
        const int vc = simulation.signalId("el.vc");
        std::vector<std::pair<sim::Tick, double>> committed;
        for (const auto& r : capture.rounds)
            for (const auto& s : r.samples)
                if (s.signal == vc)
                    committed.push_back({ s.time.tick, s.value.real });
        auto k2 = comparatorCircuit(false);
        electrical::Participant direct(k2.circuit, comparatorConfig(k2, false));
        sim::InputFrame none;
        struct NullSink final : sim::EventSink { void emit(int, sim::Tick, sim::EventPayload) override {} } sink;
        bool ok = direct.configure({}).ok && direct.initialize(0, 0, none).ok;
        sim::Tick from = 0;
        bool same = true;
        sim::OutputFrame frame;
        for (const auto& [tick, value] : committed)
        {
            if (tick == 0) continue;
            const auto r = direct.doStep(from, tick, none, sink);
            ok = ok && r.status.ok && !r.stoppedEarly;
            direct.commit();
            same = same && sameBits(direct.stepper().voltage(k2.cap), value);
            from = tick;
        }
        check(status.ok && elp->rollbacks() > 0, "the early stop rolled the electrical participant back",
              status.message + " rollbacks " + std::to_string(elp->rollbacks()));
        check(ok && same && committed.size() > 10, "its committed values equal a direct re-run through the same step times, bit for bit",
              std::to_string(committed.size()) + " samples");
    }
    {
        // Determinism: two runs record identically; a checkpoint restored
        // mid-run reproduces the rest of the run.
        auto run = [](bool checkpointAtHalf, std::vector<double>& values) {
            auto k = comparatorCircuit(false);
            sim::Simulation simulation;
            Capture capture;
            simulation.setRecorder(&capture);
            simulation.addParticipant(std::make_unique<electrical::Participant>(k.circuit, comparatorConfig(k, false)));
            auto status = simulation.configure();
            if (status) status = simulation.initialize(0);
            if (status) status = simulation.start();
            if (status) status = simulation.runUntil(us(500));
            if (checkpointAtHalf && status)
            {
                auto cp = simulation.checkpoint(status);
                if (status) status = simulation.runUntil(us(900)); // run on, then go back
                if (status) status = simulation.pause();
                if (status) status = simulation.restore(*cp);
                if (status) status = simulation.start();
            }
            if (status) status = simulation.runUntil(us(1000));
            const int vc = simulation.signalId("el.vc");
            values.clear();
            for (const auto& r : capture.rounds)
                for (const auto& s : r.samples)
                    if (s.signal == vc && s.time.tick > us(500))
                    {
                        values.push_back((double)s.time.tick);
                        values.push_back(s.value.real);
                    }
            return status;
        };
        std::vector<double> a, b, c;
        const auto sa = run(false, a), sb = run(false, b), sc = run(true, c);
        check(sa.ok && sb.ok && sameBits(a, b) && !a.empty(), "two runs commit identical values at identical times", sa.message + sb.message);
        // After the restore the recording holds both the abandoned stretch
        // and the re-run; the re-run's last part must equal the plain run.
        bool tailSame = sc.ok && c.size() >= a.size() && std::equal(a.begin(), a.end(), c.end() - (std::ptrdiff_t)a.size(),
                                                                     [](double x, double y) { return sameBits(x, y); });
        check(tailSame, "a checkpoint restored at 0.5 ms (after running on to 0.9 ms) reproduces the rest bit for bit", sc.message);
        writeValues("participant_run", a);
    }
}

// ---- 7. real-time budget ----------------------------------------------------------

// 30 nodes: a 24 V supply with an LC input filter, an H-bridge of four
// voltage-controlled switches with RC snubbers, driven by complementary
// 20 kHz PWM (70 %), a motor stand-in (1 ohm, 1 mH, 6 V back EMF) and a
// sense resistor followed by a 16-stage RC filter.
Circuit motorBridge(int& nodes)
{
    Circuit c;
    const auto bus0 = c.addNode(), bus1 = c.addNode(), bus2 = c.addNode(), a = c.addNode(), b = c.addNode(), sense = c.addNode();
    const auto m1 = c.addNode(), m2 = c.addNode(), ref = c.addNode(), pwm = c.addNode();
    c.addVoltageSource("VBUS", bus0, 0, dc(24.0));
    c.addResistor("RSRC", bus0, bus1, 0.05);
    c.addInductor("LF", bus1, bus2, 10e-6);
    c.addCapacitor("CF", bus2, 0, 470e-6);
    c.addVoltageSource("VREF", ref, 0, dc(5.0));
    Waveform p;
    p.kind = Waveform::Kind::Pulse;
    p.offset = 0.0; p.pulsed = 5.0; p.delay = 0.0; p.rise = 0.0; p.fall = 0.0; p.width = 35e-6; p.period = 50e-6;
    c.addVoltageSource("VPWM", pwm, 0, p);
    // Leg A high when PWM is high, leg B high when it is low.
    c.addVoltageControlledSwitch("S1", bus2, a, pwm, 0, 0.01, 1e9, 2.5, 0.0);
    c.addVoltageControlledSwitch("S2", a, sense, ref, pwm, 0.01, 1e9, 2.5, 0.0);
    c.addVoltageControlledSwitch("S3", bus2, b, ref, pwm, 0.01, 1e9, 2.5, 0.0);
    c.addVoltageControlledSwitch("S4", b, sense, pwm, 0, 0.01, 1e9, 2.5, 0.0);
    const std::pair<Node, Node> legs[] { { bus2, a }, { a, sense }, { bus2, b }, { b, sense } };
    for (int k = 0; k < 4; ++k)
    {
        const auto mid = c.addNode();
        c.addResistor("RS" + std::to_string(k), legs[k].first, mid, 100.0);
        c.addCapacitor("CS" + std::to_string(k), mid, legs[k].second, 1e-9);
    }
    c.addResistor("RM", a, m1, 1.0);
    c.addInductor("LM", m1, m2, 1e-3);
    c.addVoltageSource("VEMF", m2, b, dc(6.0));
    c.addResistor("RSENSE", sense, 0, 0.01);
    auto prev = sense;
    for (int k = 0; k < 16; ++k)
    {
        const auto n = c.addNode();
        c.addResistor("RF" + std::to_string(k), prev, n, 1e3);
        c.addCapacitor("CFL" + std::to_string(k), n, 0, 10e-9);
        prev = n;
    }
    nodes = c.nodeCount() - 1;
    return c;
}

void testRealTimeBudget()
{
    std::printf("-- real-time budget --\n");
    int nodes = 0;
    const auto circuit = motorBridge(nodes);
    const double simulated = 0.2; // seconds
    {
        // The stepper alone, driven as the participant drives it (20 us
        // rounds; the bridge switches are internal, so they do not end them).
        TransientStepper::Settings settings;
        settings.locateEvents = true;
        settings.timeQuantum = 1e-12;
        TransientStepper stepper(circuit, {}, settings);
        for (const char* sw : { "S1", "S2", "S3", "S4" })
            stepper.setSwitchStops(circuit.find(sw), false);
        bool ok = stepper.init();
        const auto begin = std::chrono::steady_clock::now();
        sim::Tick tick = 0;
        while (ok && tick < (sim::Tick)std::llround(simulated * 1e12))
        {
            ok = stepper.advance(sim::ticksToSeconds(tick + us(20)));
            tick = (sim::Tick)std::llround(stepper.time() * 1e12);
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::printf("      stepper alone: %.1f s simulated in %.3f s wall: %.2fx real time\n", simulated, wall, simulated / wall);
        // 4000 PWM periods, two edges each (none at t = 0, where the PWM
        // starts high), four switches flipping at every edge.
        check(ok && stepper.stats().events == 4 * (2 * 4000 - 1), "every PWM edge is located: 4 switches x 7999 edges = 31996 switch events",
              std::to_string(stepper.stats().events));
    }
    // Through the scheduler, as the system simulator runs it: five timed
    // runs (the machine is shared, so single timings vary); the median is
    // judged and the spread reported.
    electrical::Config cfg;
    cfg.name = "bridge";
    cfg.step = us(20);
    cfg.outputs.push_back({ electrical::OutputBinding::Kind::BranchCurrent, "im", 0, 0, circuit.find("LM") });
    std::vector<double> ratios;
    double im = 0.0;
    sim::Status status;
    TransientStepper::Stats st;
    std::uint64_t rounds = 0;
    for (int run = 0; run < 5 && status; ++run)
    {
        sim::Simulation simulation;
        auto el = std::make_unique<electrical::Participant>(circuit, cfg);
        auto* elp = el.get();
        simulation.addParticipant(std::move(el));
        status = simulation.configure();
        if (status) status = simulation.initialize(0);
        if (status) status = simulation.start();
        const auto begin = std::chrono::steady_clock::now();
        if (status) status = simulation.runUntil((sim::Tick)std::llround(simulated * 1e12));
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        ratios.push_back(simulated / wall);
        if (run == 0)
        {
            st = elp->stepper().stats();
            rounds = simulation.rounds();
            // The motor current, averaged over the next 20 PWM periods (sampled
            // every 2.5 us, after the timed run). Expected: (24 V x (0.7 - 0.3)
            // - 6 V) over 1 ohm + two switches + the sense resistor (1.03 ohm).
            const int imId = simulation.signalId("bridge.im");
            const auto t0 = simulation.now().tick;
            for (int k = 1; status && k <= 400; ++k)
            {
                status = simulation.runUntil(t0 + (sim::Tick)k * 2'500'000);
                im += simulation.committedValue(imId).real / 400.0;
            }
        }
    }
    auto sorted = ratios;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    const double imExpected = (24.0 * 0.4 - 6.0) / 1.03;
    std::printf("      %d nodes, 20 us steps, %.1f s simulated, 5 runs: %.2fx / %.2fx / %.2fx real time (min / median / max, %s build)\n", nodes,
                simulated, sorted.empty() ? 0.0 : sorted.front(), median, sorted.empty() ? 0.0 : sorted.back(), CIRCUIT_STEPPER_TESTS_CONFIG);
    std::printf("      per run: %llu rounds, %llu steps, %llu backward-Euler pieces, %llu location steps, %llu switch events, %llu factorisations; "
                "average motor current %.4f A (expected %.4f A)\n",
                (unsigned long long)rounds, (unsigned long long)st.steps, (unsigned long long)st.restarts,
                (unsigned long long)st.locationSteps, (unsigned long long)st.events, (unsigned long long)st.factorisations, im, imExpected);
    check(status.ok && nodes == 30, "the 30-node motor-bridge circuit runs (20 kHz PWM, located switching)", status.message);
    check(status.ok && std::abs(im - imExpected) <= 0.02 * imExpected, "the average motor current is within 2 % of the analytic 3.495 A",
          fmt("%.4f A", im));
    if (optimisedBuild)
        check(status.ok && median >= 1.0, "real-time budget: median of five runs at least 1.0x real time with the optimised libraries",
              fmt("%.3fx", median));
    else
        std::printf("      (the 1.0x requirement applies to the optimised build; reported only)\n");
}

// ---- comparison of two configurations -------------------------------------------

int compareDirs(const std::string& a, const std::string& b)
{
    std::printf("-- optimised vs unoptimised (%s vs %s) --\n", a.c_str(), b.c_str());
    const char* names[] { "rc_step", "rlc_ring", "comparator_times", "rl_restart", "save_restore", "participant_run" };
    for (const char* name : names)
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
    std::printf("circuit_sim P1 tests, library configuration: %s, output: %s\n", CIRCUIT_STEPPER_TESTS_CONFIG, outDir.c_str());
    testStepperMatchesBatch();
    testRcAndRlc();
    testComparatorLocation();
    testRestartRemovesRinging();
    testSaveRestore();
    testRollback();
    testParticipant();
    testRealTimeBudget();
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}

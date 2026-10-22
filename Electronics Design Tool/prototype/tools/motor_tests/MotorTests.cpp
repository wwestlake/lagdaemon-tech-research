// Electromechanical simulation (Source/Mechanical.*, in circuit_sim): the
// brushed DC motor on the internal solver, against analytical solutions.
//
// With constant inputs the motor is a linear system in (I, omega):
//   d/dt [I; w] = A [I; w] + b,  A = [[-R/L, -Ke/L], [Kt/J, -B/J]],  b = [V/L; -TL/J]
// whose exact solution x(t) = xss + exp(A t) (x0 - xss) is computed here
// independently of the solver (a 2x2 matrix exponential by scaling and
// squaring). Startup, steady state, load steps, reversal, generating, coast
// down, energy balance, events and switching, rollback, a closed-loop
// controller through the electrical interface, end stops and failures.
//
// Built twice, against the optimised and the unoptimised circuit_sim;
// `--compare A B` checks the two configurations agree.
//
// Usage: motor_tests [--out <dir>]
//        motor_tests --compare <dirA> <dirB>

#include "../../Source/CircuitSolver.h"
#include "../../Source/LogicGates.h"
#include "../../Source/Mechanical.h"
#include "../../Source/Pololu25DMotor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef MOTOR_TESTS_CONFIG
#define MOTOR_TESTS_CONFIG "unknown"
#endif

using namespace circuit_sim;
namespace fs = std::filesystem;

namespace
{
int failures = 0, passes = 0;
std::string outDir = "motor_test_out";

void check(bool ok, const std::string& name, const std::string& detail = {})
{
    if (ok) ++passes; else ++failures;
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), ok || detail.empty() ? "" : "  -- ", ok ? "" : detail.c_str());
    std::fflush(stdout);
}

template <typename... Args>
std::string fmt(const char* f, Args... args)
{
    char b[128];
    std::snprintf(b, sizeof b, f, args...);
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

// A step from a to b at t (over 1 us).
Waveform stepAt(double a, double b, double t) { return pwl({ { 0.0, a }, { t, a }, { t + 1e-6, b } }); }

// ---- the independent reference -----------------------------------------------------------

struct Params
{
    double r = 2.0, l = 1e-3, kt = 0.02, ke = 0.02, j = 1e-5, b = 1e-6;
};

struct State { double i = 0.0, w = 0.0; };

using M2 = std::array<std::array<double, 2>, 2>;

M2 mul(const M2& a, const M2& b)
{
    M2 c {};
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 2; ++k)
            c[r][k] = a[r][0] * b[0][k] + a[r][1] * b[1][k];
    return c;
}

// exp(A t): scale to norm < 0.5, 30 Taylor terms, square back.
M2 expm(M2 a, double t)
{
    for (auto& row : a) for (auto& v : row) v *= t;
    const auto norm = std::max(std::abs(a[0][0]) + std::abs(a[0][1]), std::abs(a[1][0]) + std::abs(a[1][1]));
    int squarings = 0;
    while (norm / std::ldexp(1.0, squarings) > 0.5) ++squarings;
    for (auto& row : a) for (auto& v : row) v = std::ldexp(v, -squarings);
    M2 e { { { 1.0, 0.0 }, { 0.0, 1.0 } } }, term = e;
    for (int k = 1; k <= 30; ++k)
    {
        term = mul(term, a);
        for (auto& row : term) for (auto& v : row) v /= k;
        for (int r = 0; r < 2; ++r) for (int c = 0; c < 2; ++c) e[r][c] += term[r][c];
    }
    for (int s = 0; s < squarings; ++s) e = mul(e, e);
    return e;
}

State steady(const Params& p, double v, double tl)
{
    const auto d = p.r * p.b + p.kt * p.ke;
    return { (p.b * v + p.ke * tl) / d, (p.kt * v - p.r * tl) / d };
}

// The motor's exact state t after `x0` with constant v and tl.
State exact(const Params& p, double v, double tl, State x0, double t)
{
    const M2 a { { { -p.r / p.l, -p.ke / p.l }, { p.kt / p.j, -p.b / p.j } } };
    const auto ss = steady(p, v, tl);
    const auto e = expm(a, t);
    const double d0 = x0.i - ss.i, d1 = x0.w - ss.w;
    return { ss.i + e[0][0] * d0 + e[0][1] * d1, ss.w + e[1][0] * d0 + e[1][1] * d1 };
}

// ---- running a motor ----------------------------------------------------------------------

struct Bench
{
    Circuit c;
    mech::DcMotor m;
    Node plus = 0;
};

Bench motorOnSource(const Params& p, Waveform v, Waveform tl = dc(0.0), mech::ShaftModel shaft = {}, double sourceR = 0.0)
{
    Bench b;
    b.plus = b.c.addNode();
    if (sourceR > 0.0)
    {
        const auto s = b.c.addNode();
        b.c.addVoltageSource("V1", s, 0, v);
        b.c.addResistor("RS", s, b.plus, sourceR);
    }
    else
        b.c.addVoltageSource("V1", b.plus, 0, v);
    mech::DcMotorModel m;
    m.ra = p.r; m.la = p.l; m.kt = p.kt; m.ke = p.ke; m.j = p.j; m.b = p.b;
    mech::MotorLoad load;
    load.torque = tl;
    b.m = mech::addDcMotor(b.c, "M1", b.plus, 0, m, load, shaft);
    return b;
}

TransientResult run(const Circuit& c, double stop, double step, const Options& o = {})
{
    TransientSettings s;
    s.stop = stop;
    s.step = step;
    s.maxSamples = 10'000'000;
    return solveTransient(c, s, o);
}

OperatingPoint sampleOp(const TransientResult& r, size_t k)
{
    OperatingPoint op;
    op.ok = true;
    op.voltages = r.voltages[k];
    op.sourceCurrents = r.sourceCurrents[k];
    return op;
}

const mech::Quantity& quantity(const mech::DcMotor& m, const std::string& key)
{
    for (const auto& q : m.quantities)
        if (q.name == "M1." + key)
            return q;
    std::printf("no quantity %s\n", key.c_str());
    std::abort();
}

double q(const Bench& b, const TransientResult& r, const std::string& key, size_t k)
{
    return mech::valueOf(quantity(b.m, key), b.c, sampleOp(r, k), r.time[k]);
}

size_t sampleAt(const TransientResult& r, double t)
{
    size_t best = 0;
    for (size_t k = 0; k < r.time.size(); ++k)
        if (std::abs(r.time[k] - t) < std::abs(r.time[best] - t))
            best = k;
    return best;
}

// Largest deviation of current and speed from the exact solution over the
// samples in [from, to], relative to the given scales.
struct Deviation { double i = 0.0, w = 0.0; };
template <typename Reference>
Deviation deviation(const Bench& b, const TransientResult& r, double from, double to, Reference ref, double iScale, double wScale)
{
    Deviation d;
    for (size_t k = 0; k < r.time.size(); ++k)
    {
        const auto t = r.time[k];
        if (t < from || t > to) continue;
        const auto x = ref(t);
        d.i = std::max(d.i, std::abs(q(b, r, "current", k) - x.i) / iScale);
        d.w = std::max(d.w, std::abs(q(b, r, "speed", k) - x.w) / wScale);
    }
    return d;
}

// Trapezoidal time integral of a quantity over the run.
double integral(const Bench& b, const TransientResult& r, const std::string& key)
{
    double s = 0.0;
    for (size_t k = 1; k < r.time.size(); ++k)
        s += 0.5 * (q(b, r, key, k) + q(b, r, key, k - 1)) * (r.time[k] - r.time[k - 1]);
    return s;
}

// ---- 1, 2. startup and steady state ---------------------------------------------------------

void testStartupAndSteadyState(std::vector<double>& out)
{
    std::printf("-- startup from rest, steady state --\n");
    const Params p;
    auto b = motorOnSource(p, dc(12.0));
    const auto r = run(b.c, 0.4, 10e-6);
    check(r.ok, "startup transient runs", r.error);
    if (!r.ok) return;
    const auto ss = steady(p, 12.0, 0.0);
    check(q(b, r, "current", 0) == 0.0 && q(b, r, "speed", 0) == 0.0 && q(b, r, "position", 0) == 0.0
          && q(b, r, "backemf", 0) == 0.0 && r.voltages[0][(size_t)b.plus] == 12.0,
          "t = 0 is the motor at rest with the source applied: I 0, speed 0, angle 0, back EMF 0, terminals 12 V",
          fmt("I %g", q(b, r, "current", 0)) + fmt(" w %g", q(b, r, "speed", 0)));
    const auto d = deviation(b, r, 0.0, 0.4, [&](double t) { return exact(p, 12.0, 0.0, {}, t); }, 6.0, ss.w);
    check(d.i < 2e-4 && d.w < 2e-4, "current and speed follow the exact solution (2nd order, both time constants) within 2e-4",
          fmt("current %.3g of the 6 A stall current", d.i) + fmt(", speed %.3g of the steady speed", d.w));
    std::printf("      largest deviation: current %.3g (of 6 A), speed %.3g (of %.6g rad/s)\n", d.i, d.w, ss.w);

    // Peak current: the electrical time constant (0.5 ms) is far faster than
    // the mechanical one (~50 ms), so the current overshoots toward 6 A.
    double peak = 0.0, tPeak = 0.0;
    for (size_t k = 0; k < r.time.size(); ++k)
        if (q(b, r, "current", k) > peak) { peak = q(b, r, "current", k); tPeak = r.time[k]; }
    double exactPeak = 0.0;
    for (double t = 0.0; t < 0.02; t += 1e-6) exactPeak = std::max(exactPeak, exact(p, 12.0, 0.0, {}, t).i);
    check(std::abs(peak - exactPeak) < 2e-3 * 6.0, fmt("startup current peak %.6g A", peak) + fmt(" at %.4g s (exact peak ", tPeak)
          + fmt("%.6g A)", exactPeak));

    const auto end = r.time.size() - 1;
    const auto xEnd = exact(p, 12.0, 0.0, {}, r.time[end]);
    check(std::abs(q(b, r, "speed", end) - xEnd.w) < 1e-6 * ss.w && std::abs(q(b, r, "current", end) - xEnd.i) < 1e-6 * 6.0,
          fmt("after 0.4 s (8 mechanical time constants): speed %.9g rad/s", q(b, r, "speed", end)) + fmt(" (exact %.9g;", xEnd.w)
          + fmt(" steady %.9g)", ss.w) + fmt(", current %.9g A", q(b, r, "current", end)) + fmt(" (exact %.9g)", xEnd.i));
    // The angle is the integral of the exact speed (Simpson, 40000 intervals).
    double theta = 0.0;
    {
        const int n = 40000;
        const auto h = 0.4 / n;
        for (int k = 0; k <= n; ++k)
            theta += (k == 0 || k == n ? 1.0 : k % 2 ? 4.0 : 2.0) * exact(p, 12.0, 0.0, {}, k * h).w;
        theta *= h / 3.0;
    }
    check(std::abs(q(b, r, "position", end) - theta) < 1e-4 * theta,
          fmt("angle after 0.4 s %.9g rad", q(b, r, "position", end)) + fmt(" = integral of the exact speed %.9g rad", theta));

    // The operating point analysis is the steady state.
    const auto op = solveOperatingPoint(b.c);
    const auto opW = op.ok ? mech::valueOf(quantity(b.m, "speed"), b.c, op, 0.0) : -1.0;
    const auto opI = op.ok ? mech::valueOf(quantity(b.m, "current"), b.c, op, 0.0) : -1.0;
    check(op.ok && std::abs(opW - ss.w) < 1e-8 * ss.w && std::abs(opI - ss.i) < 1e-7,
          fmt("operating point: steady speed %.12g rad/s", opW) + fmt(", current %.9g A", opI)
          + fmt(" (exact %.12g", ss.w) + fmt(", %.9g)", ss.i), op.error);
    check(op.ok && mech::valueOf(quantity(b.m, "position"), b.c, op, 0.0) == 0.0,
          "operating point: the angle is held at its initial value (the shaft turns at a steady speed)");

    out.insert(out.end(), { q(b, r, "speed", end), q(b, r, "current", end), q(b, r, "position", end), peak, d.i, d.w });
}

// High speed through a nonlinear part: the shaft is a quantity node, so
// Newton's voltage step limit does not hold it back.
void testHighSpeedOperatingPoint(std::vector<double>& out)
{
    Circuit c;
    const auto s = c.addNode(), plus = c.addNode();
    c.addVoltageSource("V1", s, 0, dc(12.0));
    c.addDiode("D1", s, plus);
    mech::DcMotorModel m;
    m.kt = m.ke = 0.001;
    m.b = 1e-8;
    const auto motor = mech::addDcMotor(c, "M1", plus, 0, m, {}, {});
    const auto op = solveOperatingPoint(c);
    double w = 0.0, i = 0.0, v = 0.0;
    if (op.ok)
    {
        w = op.voltages[(size_t)motor.shaft];
        i = op.sourceCurrents[(size_t)motor.armature];
        v = op.voltages[(size_t)plus];
    }
    check(op.ok && op.iterations < 60 && std::abs(v - (m.ra * i + m.ke * w)) < 1e-9 * v && std::abs(m.kt * i - m.b * w) < 1e-3 * m.b * w,
          fmt("operating point through a diode at %.6g rad/s", w) + fmt(" (%.0f rpm)", w * 60.0 / 6.283185307179586)
          + " converges in " + std::to_string(op.iterations) + " Newton iterations; V = Ra I + Ke w and Kt I = B w hold",
          op.error + fmt(" V %.12g", v) + fmt(" RaI+Kew %.12g", m.ra * i + m.ke * w) + fmt(" KtI %.6g", m.kt * i) + fmt(" Bw %.6g", m.b * w));
    out.insert(out.end(), { w, i });
}

// ---- 3, 4, 5. load, reversal, back EMF and generating ---------------------------------------------

void testLoadReversalGenerating(std::vector<double>& out)
{
    std::printf("-- load change, reversal, back EMF, generating --\n");
    const Params p;
    {
        // Load torque 0 -> 0.05 N*m at 0.2 s.
        auto b = motorOnSource(p, dc(12.0), stepAt(0.0, 0.05, 0.2));
        const auto r = run(b.c, 0.5, 10e-6);
        check(r.ok, "load step transient runs", r.error);
        if (!r.ok) return;
        const auto at02 = exact(p, 12.0, 0.0, {}, 0.2);
        const auto d = deviation(b, r, 0.2 + 2e-6, 0.5, [&](double t) { return exact(p, 12.0, 0.05, at02, t - 0.2 - 0.5e-6); }, 6.0,
                                 steady(p, 12.0, 0.0).w);
        const auto k1 = sampleAt(r, 0.2), k2 = r.time.size() - 1;
        const auto ss = exact(p, 12.0, 0.05, at02, r.time[k2] - 0.2 - 0.5e-6);
        check(q(b, r, "speed", k2) < q(b, r, "speed", k1) && q(b, r, "current", k2) > q(b, r, "current", k1),
              fmt("more load: speed %.6g", q(b, r, "speed", k1)) + fmt(" -> %.6g rad/s,", q(b, r, "speed", k2))
              + fmt(" current %.6g", q(b, r, "current", k1)) + fmt(" -> %.6g A", q(b, r, "current", k2)));
        check(d.i < 5e-4 && d.w < 5e-4 && std::abs(q(b, r, "speed", k2) - ss.w) < 1e-5 * ss.w,
              "after the load step the motor follows the exact solution from its state at 0.2 s and settles at the exact steady state",
              fmt("deviation current %.3g", d.i) + fmt(", speed %.3g", d.w));
        std::printf("      after the load step: largest deviation current %.3g, speed %.3g; at 0.5 s %.9g rad/s, %.9g A (exact %.9g, %.9g)\n",
                    d.i, d.w, q(b, r, "speed", k2), q(b, r, "current", k2), ss.w, ss.i);
        out.insert(out.end(), { q(b, r, "speed", k2), q(b, r, "current", k2) });

        // 7. Energy: what goes in is what is lost, stored or delivered.
        const auto e = [&](const std::string& key) { return integral(b, r, key); };
        const auto eIn = e("power_in"), eCu = e("power_copper"), eDamp = e("power_damping"), eLoad = e("power_load");
        const auto iEnd = q(b, r, "current", k2);
        const auto eL = 0.5 * p.l * iEnd * iEnd;
        const auto eK = q(b, r, "energy_kinetic", k2);
        const auto balance = eIn - (eCu + eDamp + eLoad + eL + eK);
        check(std::abs(balance) < 1e-4 * eIn,
              fmt("energy balance over 0.5 s: in %.9g J", eIn) + fmt(" = copper %.6g", eCu) + fmt(" + damping %.6g", eDamp)
              + fmt(" + load %.6g", eLoad) + fmt(" + inductance %.3g", eL) + fmt(" + kinetic %.6g J", eK)
              + fmt("  (residual %.3g J)", balance));
        double conv = 0.0;
        for (size_t k = 0; k < r.time.size(); ++k)
            conv = std::max(conv, std::abs(q(b, r, "power_converted", k) - q(b, r, "backemf", k) * q(b, r, "current", k)));
        check(conv < 1e-9, "electrical power converted (back EMF * I) equals mechanical (torque * speed) at every sample (Kt = Ke)",
              fmt("%.3g W", conv));
        out.insert(out.end(), { eIn, balance });
    }
    {
        // Reversal: 12 V -> -12 V at 0.2 s.
        auto b = motorOnSource(p, stepAt(12.0, -12.0, 0.2));
        const auto r = run(b.c, 0.5, 10e-6);
        check(r.ok, "reversal transient runs", r.error);
        if (!r.ok) return;
        const auto at02 = exact(p, 12.0, 0.0, {}, 0.2);
        const auto ss = steady(p, 12.0, 0.0);
        const auto d = deviation(b, r, 0.2 + 2e-6, 0.5, [&](double t) { return exact(p, -12.0, 0.0, at02, t - 0.2 - 0.5e-6); }, 12.0, ss.w);
        double tZero = -1.0;
        for (size_t k = 1; k < r.time.size(); ++k)
            if (q(b, r, "speed", k - 1) > 0.0 && q(b, r, "speed", k) <= 0.0)
            {
                const auto w0 = q(b, r, "speed", k - 1), w1 = q(b, r, "speed", k);
                tZero = r.time[k - 1] + (r.time[k] - r.time[k - 1]) * w0 / (w0 - w1);
                break;
            }
        double exactZero = -1.0;
        {
            double lo = 0.0, hi = 0.3;
            for (int it = 0; it < 200; ++it)
            {
                const auto mid = 0.5 * (lo + hi);
                (exact(p, -12.0, 0.0, at02, mid).w > 0.0 ? lo : hi) = mid;
            }
            exactZero = 0.2 + 0.5e-6 + lo;
        }
        const auto end = r.time.size() - 1;
        const auto wEnd = exact(p, -12.0, 0.0, at02, r.time[end] - 0.2 - 0.5e-6).w;
        check(std::abs(tZero - exactZero) < 1e-6 && std::abs(q(b, r, "speed", end) - wEnd) < 1e-6 * ss.w && d.w < 2e-4 && d.i < 2e-4,
              fmt("reversal: the speed passes 0 at %.7g s", tZero) + fmt(" (exact %.7g s)", exactZero)
              + fmt(" and runs at %.6g rad/s at 0.5 s", q(b, r, "speed", end)) + fmt(" (exact %.6g)", wEnd)
              + fmt("; plugging current peaks near %.3g A", -12.0 * 2 / p.r),
              fmt("deviation current %.3g", d.i) + fmt(" speed %.3g", d.w));
        out.insert(out.end(), { tZero, q(b, r, "speed", end) });
    }
    {
        // Back EMF: a spinning motor on a 1 Mohm voltmeter is a generator.
        Circuit c;
        const auto plus = c.addNode();
        c.addResistor("RM", plus, 0, 1e6);
        mech::DcMotorModel m;
        mech::ShaftModel s;
        s.omega0 = 500.0;
        const auto motor = mech::addDcMotor(c, "M1", plus, 0, m, {}, s);
        const auto r = run(c, 1.0, 1e-4);
        check(r.ok, "back-EMF transient runs", r.error);
        if (!r.ok) return;
        double worst = 0.0;
        for (size_t k = 1; k < r.time.size(); ++k)
        {
            const auto w = r.voltages[k][(size_t)motor.shaft];
            const auto i = r.sourceCurrents[k][(size_t)motor.armature];
            worst = std::max(worst, std::abs(r.voltages[k][(size_t)plus] - (m.ke * w + (m.ra) * i)) / (m.ke * 500.0));
        }
        const auto w1 = r.voltages.back()[(size_t)motor.shaft];
        // The voltmeter's current brakes it a little: B + Kt Ke / (Ra + 1 Mohm).
        const auto tau = m.j / (m.b + m.kt * m.ke / (m.ra + 1e6));
        const auto w01 = r.voltages[1][(size_t)motor.shaft];
        check(worst < 1e-9 && r.voltages[0][(size_t)plus] == 0.0
              && std::abs(r.voltages[1][(size_t)plus] - m.ke * w01 * 1e6 / (1e6 + m.ra)) < 1e-6,
              fmt("open-circuit terminals read the back EMF Ke * speed: %.6g V at 500 rad/s", r.voltages[1][(size_t)plus])
              + fmt(", %.6g V after 1 s", r.voltages.back()[(size_t)plus])
              + " (t = 0 is the held state, I = 0, so 0 V; the terminal follows within nanoseconds)", fmt("%.3g", worst));
        check(std::abs(w1 - 500.0 * std::exp(-1.0 / tau)) < 1e-4,
              fmt("and the generator coasts down with tau = J / (B + Kt Ke / (Ra + Rmeter)) = %.6g s", tau) + fmt(": %.6g rad/s at 1 s", w1)
              + fmt(" (exact %.6g)", 500.0 * std::exp(-1.0 / tau)));
        out.push_back(w1);
    }
    {
        // Generating: an overhauling load (negative load torque) drives the
        // motor past its no-load speed; the current reverses and power flows
        // back into the 12 V source.
        auto b = motorOnSource(p, dc(12.0), dc(-0.03));
        const auto r = run(b.c, 0.5, 10e-6);
        check(r.ok, "generating transient runs", r.error);
        if (!r.ok) return;
        const auto end = r.time.size() - 1;
        const auto ss = exact(p, 12.0, -0.03, {}, r.time[end]);
        check(ss.i < 0.0 && std::abs(q(b, r, "current", end) - ss.i) < 1e-6 && std::abs(q(b, r, "speed", end) - ss.w) < 1e-6 * ss.w
              && q(b, r, "power_in", end) < 0.0 && q(b, r, "backemf", end) > 12.0,
              fmt("regenerating: speed %.6g rad/s (above the 600 rad/s no-load speed),", q(b, r, "speed", end))
              + fmt(" current %.6g A,", q(b, r, "current", end)) + fmt(" back EMF %.6g V,", q(b, r, "backemf", end))
              + fmt(" power into the terminals %.6g W", q(b, r, "power_in", end)) + fmt(" (exact %.6g A)", ss.i));
        out.insert(out.end(), { q(b, r, "speed", end), q(b, r, "current", end) });
    }
}

// ---- 6. inertia, coast down, friction, end stops ----------------------------------------------------

void testInertiaAndFriction(std::vector<double>& out)
{
    std::printf("-- inertia, coast down, friction, end stops --\n");
    {
        // Load inertia doubles J: the exact solution with J = 2e-5.
        Params p;
        Bench b;
        b.plus = b.c.addNode();
        b.c.addVoltageSource("V1", b.plus, 0, dc(12.0));
        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.inertia = 1e-5;
        b.m = mech::addDcMotor(b.c, "M1", b.plus, 0, m, load, {});
        const auto r = run(b.c, 0.4, 10e-6);
        Params p2 = p;
        p2.j = 2e-5;
        const auto d = r.ok ? deviation(b, r, 0.0, 0.4, [&](double t) { return exact(p2, 12.0, 0.0, {}, t); }, 6.0, 600.0) : Deviation { 1, 1 };
        const auto k = r.ok ? sampleAt(r, 0.05) : 0;
        check(r.ok && d.i < 2e-4 && d.w < 2e-4,
              fmt("inertial load (J doubled): speed %.6g rad/s at 50 ms", r.ok ? q(b, r, "speed", k) : 0.0)
              + fmt(" (exact %.6g; ", exact(p2, 12.0, 0.0, {}, 0.05).w) + fmt("%.6g without the load)", exact(p, 12.0, 0.0, {}, 0.05).w),
              r.error + fmt(" deviation i %.3g", d.i) + fmt(" w %.3g", d.w));
    }
    {
        // Viscous coast down, terminals open (1 Gohm): w = w0 e^(-t/tau),
        // angle = w0 tau (1 - e^(-t/tau)).
        Circuit c;
        const auto plus = c.addNode();
        c.addResistor("RO", plus, 0, 1e9);
        mech::DcMotorModel m;
        m.b = 1e-4;
        mech::ShaftModel s;
        s.omega0 = 400.0;
        s.theta0 = 1.0;
        const auto motor = mech::addDcMotor(c, "M1", plus, 0, m, {}, s);
        const auto r = run(c, 0.5, 20e-6);
        const auto tau = m.j / m.b;
        double worstW = 0.0, worstTheta = 0.0;
        if (r.ok)
            for (size_t k = 0; k < r.time.size(); ++k)
            {
                const auto t = r.time[k];
                worstW = std::max(worstW, std::abs(r.voltages[k][(size_t)motor.shaft] - 400.0 * std::exp(-t / tau)));
                worstTheta = std::max(worstTheta, std::abs(r.voltages[k][(size_t)motor.angle] - (1.0 + 400.0 * tau * (1.0 - std::exp(-t / tau)))));
            }
        check(r.ok && worstW < 1e-3 && worstTheta < 1e-4,
              fmt("viscous coast down from 400 rad/s, tau %.3g s: speed and angle (from 1 rad) match the exact exponential", tau)
              + fmt(" (largest error %.3g rad/s,", worstW) + fmt(" %.3g rad)", worstTheta), r.error);
        out.insert(out.end(), { worstW, worstTheta });
    }
    {
        // Coulomb friction, no viscous damping: a linear decline at Tc/J,
        // then the shaft stops (it does not reverse).
        Circuit c;
        const auto plus = c.addNode();
        c.addResistor("RO", plus, 0, 1e9);
        mech::DcMotorModel m;
        m.b = 0.0;
        mech::ShaftModel s;
        s.omega0 = 500.0;
        s.coulomb = 1e-3;
        s.smoothing = 0.1;
        const auto motor = mech::addDcMotor(c, "M1", plus, 0, m, {}, s);
        const auto r = run(c, 7.0, 1e-3);
        check(r.ok, "Coulomb coast down runs", r.error);
        if (!r.ok) return;
        auto w = [&](double t) { return r.voltages[sampleAt(r, t)][(size_t)motor.shaft]; };
        double lowest = 1e9;
        for (const auto& v : r.voltages) lowest = std::min(lowest, v[(size_t)motor.shaft]);
        check(std::abs(w(2.0) - 300.0) < 0.01 && std::abs(w(4.0) - 100.0) < 0.01 && std::abs(w(6.0)) < 1e-3 && lowest > -1e-3,
              fmt("Coulomb friction 1 mN*m on 1e-5 kg*m^2: %.6g rad/s at 2 s (exact 300),", w(2.0)) + fmt(" %.6g at 4 s (100),", w(4.0))
              + fmt(" %.3g at 6 s (stopped at 5 s), never reverses", w(6.0)), fmt("lowest %.3g", lowest));
        const auto theta = r.voltages.back()[(size_t)motor.angle];
        check(std::abs(theta - 1250.0) < 0.5, fmt("angle when stopped %.6g rad", theta) + " (exact 1250 rad = 500^2 / (2 * 100 rad/s^2))");
        out.insert(out.end(), { w(2.0), w(4.0), theta });
    }
    {
        // End stop at 2 rad: the motor drives into it and stalls there.
        const Params p;
        mech::ShaftModel s;
        s.hasMax = true;
        s.maxAngle = 2.0;
        s.stopStiffness = 100.0;
        s.stopDamping = 0.05;
        auto b = motorOnSource(p, dc(12.0), dc(0.0), s);
        const auto r = run(b.c, 0.3, 10e-6);
        check(r.ok, "end-stop transient runs", r.error);
        if (!r.ok) return;
        const auto end = r.time.size() - 1;
        double most = 0.0;
        for (size_t k = 0; k < r.time.size(); ++k) most = std::max(most, q(b, r, "position", k));
        const auto stall = p.kt * 12.0 / p.r;
        check(std::abs(q(b, r, "speed", end)) < 1e-6 && std::abs(q(b, r, "current", end) - 6.0) < 1e-5
              && std::abs(q(b, r, "position", end) - (2.0 + stall / 100.0)) < 1e-6,
              fmt("end stop at 2 rad: the shaft stalls at %.7g rad", q(b, r, "position", end))
              + fmt(" (2 + stall torque / stiffness = %.7g),", 2.0 + stall / 100.0) + fmt(" speed %.3g,", q(b, r, "speed", end))
              + fmt(" stall current %.7g A (V/Ra = 6)", q(b, r, "current", end)) + fmt("; furthest %.5g rad", most));
        out.insert(out.end(), { q(b, r, "position", end), most });
    }
}

// ---- 8. switching, events, a logic-gate drive ------------------------------------------------------------

void testSwitchingAndEvents(std::vector<double>& out)
{
    std::printf("-- events and switching --\n");
    {
        // A buffer gate (output 0 / 12 V behind 0.5 ohm) switches the motor
        // on: its input crosses 2.5 V at 1.0005 ms, the output starts at
        // + tpd = 1.00051 ms (a device event the stepper lands on). The motor
        // current is exactly 0 up to that sample, then follows the exact
        // startup with R = Ra + 0.5 ohm.
        Circuit c;
        const auto in = c.addNode(), out1 = c.addNode();
        c.addVoltageSource("VIN", in, 0, pwl({ { 0.0, 0.0 }, { 1e-3, 0.0 }, { 1.001e-3, 5.0 } }));
        logic::Model g;
        g.voh = 12.0;
        g.rOut = 0.5;
        c.addProgrammable("U1", { in, out1 }, std::make_shared<logic::GateDevice>(logic::Function::Buffer, 1, g));
        mech::DcMotorModel m;
        const auto motor = mech::addDcMotor(c, "M1", out1, 0, m, {}, {});
        const auto r = run(c, 0.1, 10e-6);
        check(r.ok, "gate-driven motor transient runs", r.error);
        if (!r.ok) return;
        const auto edge = 1.0005e-3 + 10e-9;
        // Before the edge the output is the gate's low level; its DC
        // operating point is a smooth transfer curve, so "0" is ~1e-14 A here.
        bool landed = false, zeroBefore = true;
        double before = 0.0;
        for (size_t k = 0; k < r.time.size(); ++k)
        {
            if (std::abs(r.time[k] - edge) < 1e-15) landed = true;
            if (r.time[k] <= edge) before = std::max(before, std::abs(r.sourceCurrents[k][(size_t)motor.armature]));
        }
        zeroBefore = before < 1e-12;
        Params p;
        p.r = m.ra + 0.5;
        double worst = 0.0;
        for (size_t k = 0; k < r.time.size(); ++k)
            if (r.time[k] > edge + 1e-5)
            {
                const auto x = exact(p, 12.0, 0.0, {}, r.time[k] - edge - 2e-9);
                worst = std::max(worst, std::abs(r.sourceCurrents[k][(size_t)motor.armature] - x.i) / 4.8);
            }
        check(landed && zeroBefore && worst < 5e-4,
              "gate output edge at exactly 1.00051 ms (input crossing + tpd): the motor current is 0 through that sample, then "
              "follows the exact startup",
              std::string(landed ? "" : "no sample at the edge; ") + fmt("largest current up to the edge %.3g A; ", before) + fmt("deviation %.3g", worst));
        out.push_back(worst);
    }
    {
        // PWM through a switch with a freewheeling diode, 5 kHz, 50 %. The
        // motor is linear, so its periodic steady state averages to the DC
        // response to the average terminal voltage.
        Circuit c;
        const auto s = c.addNode(), plus = c.addNode(), ctl = c.addNode();
        c.addVoltageSource("V1", s, 0, dc(12.0));
        Waveform sq;
        sq.kind = Waveform::Kind::Square;
        sq.offset = 2.5;
        sq.amplitude = 2.5;
        sq.frequency = 5000.0;
        sq.duty = 0.5;
        c.addVoltageSource("VC", ctl, 0, sq);
        c.addVoltageControlledSwitch("S1", s, plus, ctl, 0, 0.01, 1e9, 2.5, 0.0);
        c.addDiode("D1", 0, plus);
        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.torque = dc(0.03);
        const auto motor = mech::addDcMotor(c, "M1", plus, 0, m, load, {});
        const auto r = run(c, 0.4, 2e-6);
        check(r.ok, "PWM transient runs", r.error);
        if (!r.ok) return;
        double tv = 0.0, tw = 0.0, span = 0.0;
        for (size_t k = 1; k < r.time.size(); ++k)
        {
            if (r.time[k - 1] < 0.3) continue;
            const auto dt = r.time[k] - r.time[k - 1];
            tv += 0.5 * dt * (r.voltages[k][(size_t)plus] + r.voltages[k - 1][(size_t)plus]);
            tw += 0.5 * dt * (r.voltages[k][(size_t)motor.shaft] + r.voltages[k - 1][(size_t)motor.shaft]);
            span += dt;
        }
        const auto vAvg = tv / span, wAvg = tw / span;
        const auto wDc = steady(Params {}, vAvg, 0.03).w;
        check(std::abs(wAvg - wDc) < 2e-3 * wDc && vAvg > 4.0 && vAvg < 6.5,
              fmt("PWM 50 %%, 30 mN*m load: average terminal voltage %.6g V (12 V on, the diode's drop off),", vAvg)
              + fmt(" average speed %.6g rad/s", wAvg) + fmt(" = the steady speed at that voltage %.6g rad/s", wDc),
              fmt("speed at 0.2 s %.6g", r.voltages[sampleAt(r, 0.2)][(size_t)motor.shaft])
              + fmt(", 0.3 s %.6g", r.voltages[sampleAt(r, 0.3)][(size_t)motor.shaft]));
        out.insert(out.end(), { vAvg, wAvg });
    }
}

// ---- 9. rollback and determinism ------------------------------------------------------------------------

void testRollback(std::vector<double>& out)
{
    std::printf("-- rollback and deterministic reruns --\n");
    const Params p;
    mech::ShaftModel s;
    s.coulomb = 2e-3;
    auto b = motorOnSource(p, stepAt(12.0, 6.0, 0.02), stepAt(0.0, 0.02, 0.03), s);
    const auto r1 = run(b.c, 0.06, 10e-6), r2 = run(b.c, 0.06, 10e-6);
    bool same = r1.ok && r2.ok && r1.time.size() == r2.time.size();
    for (size_t k = 0; same && k < r1.time.size(); ++k)
        same = sameBits(r1.voltages[k], r2.voltages[k]) && sameBits(r1.sourceCurrents[k], r2.sourceCurrents[k]);
    check(same, "two runs of the same circuit are bit-identical (speed, angle, current, every node)");

    TransientStepper stepper(b.c);
    bool ok = stepper.init();
    for (int k = 1; ok && k <= 2500; ++k) ok = stepper.advance(k * 10e-6);
    TransientStepper::State saved;
    ok = ok && stepper.saveState(saved);
    std::vector<double> first, second;
    for (int k = 2501; ok && k <= 3500; ++k) ok = stepper.advance(k * 10e-6);
    first = stepper.nodeVoltages();
    const auto firstI = stepper.sourceCurrents();
    ok = ok && stepper.restoreState(saved);
    const auto restoredAngle = stepper.voltage(b.m.angle);
    for (int k = 2501; ok && k <= 3500; ++k) ok = stepper.advance(k * 10e-6);
    second = stepper.nodeVoltages();
    check(ok && sameBits(first, second) && sameBits(firstI, stepper.sourceCurrents()) && restoredAngle < first[(size_t)b.m.angle],
          fmt("rolled back from 35 ms to 25 ms (angle %.9g rad) and re-stepped: bit-identical speed, angle, current at 35 ms", restoredAngle),
          stepper.error());
    // The stepper and solveTransient take the same steps.
    const auto k35 = sampleAt(r1, 0.035);
    check(ok && r1.ok && std::abs(first[(size_t)b.m.shaft] - r1.voltages[k35][(size_t)b.m.shaft]) < 1e-9 * 600.0,
          fmt("the stepper's speed at 35 ms %.12g", first[(size_t)b.m.shaft]) + fmt(" = solveTransient's %.12g", r1.voltages[k35][(size_t)b.m.shaft]));
    out.insert(out.end(), { first[(size_t)b.m.shaft], first[(size_t)b.m.angle] });
}

// ---- 11. a controller through the electrical interface --------------------------------------------------------

// A proportional speed controller: reads a tachometer voltage on pin 0,
// drives pin 1 to Kp * (setpoint - tach) clamped to 0..12 V behind 0.1 ohm.
class Controller final : public ProgrammableDevice
{
public:
    void reset() override {}
    bool evaluate(double, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        const auto drive = std::clamp(kp * (setpoint - v[0]), 0.0, 12.0);
        i = { -v[0] / 1e6, (drive - v[1]) / 0.1 };
        return true;
    }
    void accept(double) override {}
    bool saveState(std::vector<std::uint8_t>& out) const override { out.clear(); return true; }
    bool restoreState(const std::vector<std::uint8_t>&) override { return true; }
    double kp = 20.0, setpoint = 3.0;
};

void testController(std::vector<double>& out)
{
    std::printf("-- closed loop: a controller reading a tachometer --\n");
    Circuit c;
    const auto drive = c.addNode(), tach = c.addNode();
    auto ctl = std::make_shared<Controller>();
    c.addProgrammable("U1", { tach, drive }, ctl);
    mech::DcMotorModel m;
    const auto motor = mech::addDcMotor(c, "M1", drive, 0, m, {}, {});
    mech::addTachometer(c, "TG", motor.shaft, tach, 0, 0.01, 100.0);
    const auto r = run(c, 0.5, 10e-6);
    check(r.ok, "closed-loop transient runs", r.error);
    if (!r.ok) return;
    // Steady state: V = Kp (S - k w) (unclamped), w = c1 V, c1 = Kt / (Ra B + Kt Ke);
    // the tach output carries only the controller's 1 Mohm, so it reads k w (1 - 1e-4).
    const auto c1 = m.kt / (m.ra * m.b + m.kt * m.ke);
    const auto k = 0.01 * 1e6 / (1e6 + 100.0);
    const auto rTotal = m.ra + 0.1;
    const auto c1r = m.kt / (rTotal * m.b + m.kt * m.ke); // the 0.1 ohm drive resistance
    (void)c1;
    const auto wExact = c1r * ctl->kp * ctl->setpoint / (1.0 + c1r * ctl->kp * k);
    const auto w = r.voltages.back()[(size_t)motor.shaft];
    double peak = 0.0;
    for (const auto& v : r.voltages) peak = std::max(peak, v[(size_t)drive]);
    check(std::abs(w - wExact) < 1e-6 * wExact && peak > 11.9,
          fmt("the controller regulates the speed through the tachometer: %.9g rad/s", w) + fmt(" (exact %.9g;", wExact)
          + fmt(" the drive saturates at %.4g V while starting)", peak));
    out.push_back(w);
}

// ---- 11b. closed-loop PID speed control with anti-windup & disturbance recovery ----------------

class PidSpeedController final : public ProgrammableDevice
{
public:
    void reset() override
    {
        integral = 0.0;
        lastError = 0.0;
        lastT = 0.0;
        trialIntegral = 0.0;
        trialError = 0.0;
        hasLast = false;
    }

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        const double wMeas = v[0] / kv;
        const double wTarget = (setpointFn ? setpointFn(t) : setpoint);
        const double err = wTarget - wMeas;
        const double dt = (hasLast && t > lastT) ? (t - lastT) : 0.0;

        const double pTerm = kp * err;
        const double dTerm = (hasLast && dt > 1e-9) ? kd * (err - lastError) / dt : 0.0;

        // Anti-windup conditional integration
        const double uUnclamped = pTerm + integral * ki + dTerm;
        const bool satHigh = (uUnclamped >= vMax);
        const bool satLow = (uUnclamped <= vMin);
        const bool integrate = (!satHigh && !satLow) || (satHigh && err < 0.0) || (satLow && err > 0.0);

        trialIntegral = integral + (integrate ? err * dt : 0.0);
        trialError = err;

        const double u = std::clamp(pTerm + trialIntegral * ki + dTerm, vMin, vMax);
        i = { -v[0] / 1e6, (u - v[1]) / rOut };
        return true;
    }

    void accept(double t) override
    {
        integral = trialIntegral;
        lastError = trialError;
        lastT = t;
        hasLast = true;
    }

    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        out.resize(sizeof(double) * 3 + sizeof(bool));
        std::uint8_t* ptr = out.data();
        std::memcpy(ptr, &integral, sizeof(double)); ptr += sizeof(double);
        std::memcpy(ptr, &lastError, sizeof(double)); ptr += sizeof(double);
        std::memcpy(ptr, &lastT, sizeof(double)); ptr += sizeof(double);
        std::memcpy(ptr, &hasLast, sizeof(bool));
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() < sizeof(double) * 3 + sizeof(bool)) return false;
        const std::uint8_t* ptr = in.data();
        std::memcpy(&integral, ptr, sizeof(double)); ptr += sizeof(double);
        std::memcpy(&lastError, ptr, sizeof(double)); ptr += sizeof(double);
        std::memcpy(&lastT, ptr, sizeof(double)); ptr += sizeof(double);
        std::memcpy(&hasLast, ptr, sizeof(bool));
        trialIntegral = integral;
        trialError = lastError;
        return true;
    }

    double kp = 0.08;
    double ki = 2.0;
    double kd = 0.0002;
    double kv = 0.01;
    double rOut = 0.05;
    double vMin = 0.0, vMax = 12.0;
    double setpoint = 300.0;
    std::function<double(double)> setpointFn;

    double integral = 0.0;
    double lastError = 0.0;
    double lastT = 0.0;
    double trialIntegral = 0.0;
    double trialError = 0.0;
    bool hasLast = false;
};

void testClosedLoopSpeedControl(std::vector<double>& out)
{
    std::printf("-- closed loop: PID speed controller with anti-windup & disturbance recovery --\n");
    Circuit c;
    const auto drive = c.addNode(), tach = c.addNode();
    auto ctl = std::make_shared<PidSpeedController>();
    ctl->setpointFn = [](double t) { return (t < 0.7) ? 300.0 : 450.0; };
    c.addProgrammable("U_PID", { tach, drive }, ctl);

    mech::DcMotorModel m;
    // Load disturbance: 0.03 N*m applied from 0.25 s to 0.50 s
    mech::MotorLoad load;
    load.torque = pwl({ { 0.0, 0.0 }, { 0.25, 0.0 }, { 0.250001, 0.03 }, { 0.50, 0.03 }, { 0.500001, 0.0 } });
    const auto motor = mech::addDcMotor(c, "M1", drive, 0, m, load, {});
    mech::addTachometer(c, "TG", motor.shaft, tach, 0, 0.01, 100.0);

    const auto r = run(c, 1.0, 10e-6);
    check(r.ok, "PID speed control transient completed", r.error);
    if (!r.ok) return;

    // 1. Startup: accelerates and stabilizes at 300 rad/s before disturbance
    const auto kPre = sampleAt(r, 0.20);
    const auto wPre = r.voltages[kPre][(size_t)motor.shaft];
    check(std::abs(wPre - 300.0) < 0.5,
          fmt("stabilizes at 300 rad/s setpoint before load: %.6g rad/s", wPre));

    // 2. Disturbance applied at 0.25 s: speed dips temporarily
    const auto kDip = sampleAt(r, 0.26);
    const auto wDip = r.voltages[kDip][(size_t)motor.shaft];
    check(wDip < 290.0, fmt("load disturbance dips speed to %.6g rad/s", wDip));

    // 3. Compensation: integral action eliminates steady-state droop under 0.03 N*m load
    const auto kUnderLoad = sampleAt(r, 0.48);
    const auto wUnderLoad = r.voltages[kUnderLoad][(size_t)motor.shaft];
    check(std::abs(wUnderLoad - 300.0) < 0.5,
          fmt("integrator compensates 0.03 N*m load (zero steady-state droop): %.6g rad/s", wUnderLoad));

    // 4. Load removal at 0.50 s: recovers back to 300 rad/s
    const auto kPost = sampleAt(r, 0.68);
    const auto wPost = r.voltages[kPost][(size_t)motor.shaft];
    check(std::abs(wPost - 300.0) < 0.5,
          fmt("recovers to 300 rad/s after load removed: %.6g rad/s", wPost));

    // 5. Setpoint step to 450 rad/s at 0.70 s: accelerates and stabilizes
    const auto kNewSet = sampleAt(r, 0.98);
    const auto wNewSet = r.voltages[kNewSet][(size_t)motor.shaft];
    check(std::abs(wNewSet - 450.0) < 0.5,
          fmt("tracks new setpoint step to 450 rad/s: %.6g rad/s", wNewSet));

    out.insert(out.end(), { wPre, wDip, wUnderLoad, wPost, wNewSet });
}

// ---- 11c. closed-loop position control with velocity damping & tolerance deadband ----------------

class PositionController final : public ProgrammableDevice
{
public:
    void reset() override {}

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        const double pos = v[0] / kpos;
        const double speed = v[1] / kv;
        const double target = (targetFn ? targetFn(t) : targetAngle);
        const double err = target - pos;

        // PD control with critical damping
        const double uUnclamped = kp * err - kd * speed;
        const double u = std::clamp(uUnclamped, vMin, vMax);
        i = { -v[0] / 1e6, -v[1] / 1e6, (u - v[2]) / rOut };
        return true;
    }

    void accept(double) override {}
    bool saveState(std::vector<std::uint8_t>& out) const override { out.clear(); return true; }
    bool restoreState(const std::vector<std::uint8_t>&) override { return true; }

    double kp = 50.0;
    double kd = 0.28;
    double kpos = 1.0;
    double kv = 0.01;
    double rOut = 0.05;
    double vMin = -12.0, vMax = 12.0;
    double tolerance = 0.015;
    double targetAngle = 0.0;
    std::function<double(double)> targetFn;
};

void testClosedLoopPositionControl(std::vector<double>& out)
{
    std::printf("-- closed loop: bidirectional position control with damping & holding --\n");
    Circuit c;
    const auto drive = c.addNode(), tach = c.addNode(), posNode = c.addNode();
    auto ctl = std::make_shared<PositionController>();
    ctl->targetFn = [](double t) {
        if (t < 0.35) return 2.0;
        if (t < 0.70) return -1.5;
        return 3.5;
    };
    c.addProgrammable("U_POS", { posNode, tach, drive }, ctl);

    mech::DcMotorModel m;
    // Holding load torque applied during target 3.5: 0.005 N*m at t >= 0.70
    mech::MotorLoad load;
    load.torque = pwl({ { 0.0, 0.0 }, { 0.70, 0.0 }, { 0.700001, 0.005 } });
    mech::ShaftModel s;
    const auto motor = mech::addDcMotor(c, "M1", drive, 0, m, load, s);
    mech::addTachometer(c, "TG", motor.shaft, tach, 0, 0.01, 100.0);
    mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);

    const auto r = run(c, 1.0, 10e-6);
    check(r.ok, "position control transient completed", r.error);
    if (!r.ok) return;

    // 1. Forward motion to +2.0 rad: settles within tolerance +/- 0.015 rad
    const auto k1 = sampleAt(r, 0.32);
    const auto th1 = r.voltages[k1][(size_t)motor.angle];
    const auto w1 = r.voltages[k1][(size_t)motor.shaft];
    check(std::abs(th1 - 2.0) <= ctl->tolerance && std::abs(w1) < 0.2,
          fmt("target +2.0 rad: settled at %.6g rad (|err| <= 0.015 rad), speed %.6g rad/s", th1, w1));

    // 2. Reverse motion to -1.5 rad: reverses and settles within tolerance
    const auto k2 = sampleAt(r, 0.67);
    const auto th2 = r.voltages[k2][(size_t)motor.angle];
    const auto w2 = r.voltages[k2][(size_t)motor.shaft];
    check(std::abs(th2 - (-1.5)) <= ctl->tolerance && std::abs(w2) < 0.2,
          fmt("target -1.5 rad (reverse): settled at %.6g rad, speed %.6g rad/s", th2, w2));

    // 3. Move to +3.5 rad under holding load: maintains position within tolerance
    const auto k3 = sampleAt(r, 0.98);
    const auto th3 = r.voltages[k3][(size_t)motor.angle];
    const auto w3 = r.voltages[k3][(size_t)motor.shaft];
    check(std::abs(th3 - 3.5) <= ctl->tolerance && std::abs(w3) < 0.2,
          fmt("target +3.5 rad with holding load: holds at %.6g rad, speed %.6g rad/s", th3, w3));

    // Check voltage clamping
    double maxV = 0.0, minV = 0.0;
    for (const auto& vRow : r.voltages)
    {
        maxV = std::max(maxV, vRow[(size_t)drive]);
        minV = std::min(minV, vRow[(size_t)drive]);
    }
    check(maxV <= 12.0001 && minV >= -12.0001,
          fmt("drive voltage strictly clamped within limits: [%.4g, %.4g] V", minV, maxV));

    out.insert(out.end(), { th1, th2, th3, maxV, minV });
}

// ---- 11d. coordinated door automation (CloseAndLockDoor) ----------------------------------------

class DoorAutomationSequencer final : public ProgrammableDevice
{
public:
    enum class State { Idle = 0, Closing = 1, Settling = 2, Locking = 3, Secured = 4, FaultStall = 5, FaultLock = 6, Aborted = 7 };

    void reset() override
    {
        state = State::Idle;
        tClosed = 0.0;
        tLockStart = 0.0;
        trialState = state;
        trialTClosed = 0.0;
        trialTLockStart = 0.0;
    }

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        const double pos = v[0];
        const double lockSens = v[1];
        trialState = state;
        trialTClosed = tClosed;
        trialTLockStart = tLockStart;

        if (abortAt > 0.0 && t >= abortAt && (state == State::Closing || state == State::Settling || state == State::Locking))
        {
            trialState = State::Aborted;
        }
        else if (preemptAt > 0.0 && t >= preemptAt && state == State::Closing)
        {
            trialState = State::Idle;
        }
        else
        {
            switch (state)
            {
                case State::Idle:
                    if (startAt >= 0.0 && t >= startAt)
                        trialState = State::Closing;
                    break;
                case State::Closing:
                    if (pos <= closedThreshold)
                    {
                        trialState = State::Settling;
                        trialTClosed = t;
                    }
                    else if (t - startAt > closeTimeout)
                    {
                        trialState = State::FaultStall;
                    }
                    break;
                case State::Settling:
                    if (t >= tClosed + settleDelay)
                    {
                        trialState = State::Locking;
                        trialTLockStart = t;
                    }
                    break;
                case State::Locking:
                    if (lockSens >= 2.5)
                    {
                        trialState = State::Secured;
                    }
                    else if (t - tLockStart > lockTimeout)
                    {
                        trialState = State::FaultLock;
                    }
                    break;
                case State::Secured:
                case State::FaultStall:
                case State::FaultLock:
                case State::Aborted:
                    break;
            }
        }

        double vMotor = 0.0;
        double vLock = 0.0;
        if (trialState == State::Closing)
            vMotor = -3.0; // gentle closing drive
        if (trialState == State::Locking || trialState == State::Secured)
            vLock = 12.0;

        i = { -v[0] / 1e6, -v[1] / 1e6, (vMotor - v[2]) / 0.05, (vLock - v[3]) / 0.1 };
        return true;
    }

    void accept(double) override
    {
        state = trialState;
        tClosed = trialTClosed;
        tLockStart = trialTLockStart;
    }

    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        out.resize(sizeof(State) + sizeof(double) * 2);
        std::uint8_t* p = out.data();
        std::memcpy(p, &state, sizeof(State)); p += sizeof(State);
        std::memcpy(p, &tClosed, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &tLockStart, sizeof(double));
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() < sizeof(State) + sizeof(double) * 2) return false;
        const std::uint8_t* p = in.data();
        std::memcpy(&state, p, sizeof(State)); p += sizeof(State);
        std::memcpy(&tClosed, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&tLockStart, p, sizeof(double));
        trialState = state;
        trialTClosed = tClosed;
        trialTLockStart = tLockStart;
        return true;
    }

    State state = State::Idle;
    double tClosed = 0.0;
    double tLockStart = 0.0;
    State trialState = State::Idle;
    double trialTClosed = 0.0;
    double trialTLockStart = 0.0;

    double startAt = 0.0;
    double abortAt = -1.0;
    double preemptAt = -1.0;
    double closedThreshold = 0.02;
    double settleDelay = 0.045; // 45 ms settling delay
    double closeTimeout = 0.35;
    double lockTimeout = 0.05;
};

void testCoordinatedDoorAutomation(std::vector<double>& out)
{
    std::printf("-- coordinated automation: CloseAndLockDoor sequencing & fault handling --\n");

    // Scenario 1: Normal close and lock cycle
    {
        Circuit c;
        const auto posNode = c.addNode(), lockSens = c.addNode(), motorDrive = c.addNode(), lockDrive = c.addNode();
        auto seq = std::make_shared<DoorAutomationSequencer>();
        c.addProgrammable("SEQ", { posNode, lockSens, motorDrive, lockDrive }, seq);

        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.damping = 2e-3; // door hinge damping
        mech::ShaftModel s;
        s.theta0 = 1.0; // Door starts open at 1.0 rad
        s.hasMin = true; s.minAngle = 0.0; // Door frame stops at 0 rad
        const auto motor = mech::addDcMotor(c, "M_DOOR", motorDrive, 0, m, load, s);
        mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);

        // Lock sensor engages when solenoid is energized (physical RC delay: R=10k, C=2uF -> tau=20ms)
        c.addResistor("R_BOLT", lockDrive, lockSens, 10e3);
        c.addCapacitor("C_BOLT", lockSens, 0, 2e-6);

        const auto r = run(c, 0.4, 10e-6);
        check(r.ok && seq->state == DoorAutomationSequencer::State::Secured,
              fmt("normal cycle: door closes, settles %.4g ms, locks and confirms SECURED",
                  (seq->tLockStart - seq->tClosed) * 1e3));
        check(seq->tLockStart - seq->tClosed >= 0.040 && seq->tLockStart - seq->tClosed <= 0.050,
              fmt("settling delay is between 40 and 50 ms (actual: %.4g ms)", (seq->tLockStart - seq->tClosed) * 1e3));
        out.push_back((double)seq->state);
        out.push_back(seq->tClosed);
        out.push_back(seq->tLockStart);
    }

    // Scenario 2: Obstruction / Stall / Timeout (door blocked at 0.5 rad)
    {
        Circuit c;
        const auto posNode = c.addNode(), lockSens = c.addNode(), motorDrive = c.addNode(), lockDrive = c.addNode();
        auto seq = std::make_shared<DoorAutomationSequencer>();
        seq->closeTimeout = 0.20;
        c.addProgrammable("SEQ", { posNode, lockSens, motorDrive, lockDrive }, seq);

        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.damping = 2e-3;
        mech::ShaftModel s;
        s.theta0 = 1.0;
        s.hasMin = true; s.minAngle = 0.5; // Obstruction stops door at 0.5 rad
        const auto motor = mech::addDcMotor(c, "M_DOOR", motorDrive, 0, m, load, s);
        mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);
        c.addResistor("R_PULLDOWN", lockSens, 0, 10e3);

        const auto r = run(c, 0.3, 10e-6);
        check(r.ok && seq->state == DoorAutomationSequencer::State::FaultStall,
              "door obstruction: motor stalled/timed out, detected FaultStall");
        out.push_back((double)seq->state);
    }

    // Scenario 3: Lock engagement failure (bolt fails to engage / sensor never asserts)
    {
        Circuit c;
        const auto posNode = c.addNode(), lockSens = c.addNode(), motorDrive = c.addNode(), lockDrive = c.addNode();
        auto seq = std::make_shared<DoorAutomationSequencer>();
        c.addProgrammable("SEQ", { posNode, lockSens, motorDrive, lockDrive }, seq);

        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.damping = 2e-3;
        mech::ShaftModel s;
        s.theta0 = 1.0;
        s.hasMin = true; s.minAngle = 0.0;
        const auto motor = mech::addDcMotor(c, "M_DOOR", motorDrive, 0, m, load, s);
        mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);
        c.addResistor("R_PULLDOWN", lockSens, 0, 10e3); // Jammed bolt: sensor stays at 0V!

        const auto r = run(c, 0.4, 10e-6);
        check(r.ok && seq->state == DoorAutomationSequencer::State::FaultLock,
              "lock sensor failure: bolt did not confirm within timeout, detected FaultLock");
        out.push_back((double)seq->state);
    }

    // Scenario 4: Abort / Cancel command mid-cycle (at t = 0.04 s)
    {
        Circuit c;
        const auto posNode = c.addNode(), lockSens = c.addNode(), motorDrive = c.addNode(), lockDrive = c.addNode();
        auto seq = std::make_shared<DoorAutomationSequencer>();
        seq->abortAt = 0.04; // Cancel command arrives while door is in transit
        c.addProgrammable("SEQ", { posNode, lockSens, motorDrive, lockDrive }, seq);

        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.damping = 2e-3;
        mech::ShaftModel s;
        s.theta0 = 1.0;
        s.hasMin = true; s.minAngle = 0.0;
        const auto motor = mech::addDcMotor(c, "M_DOOR", motorDrive, 0, m, load, s);
        mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);
        c.addResistor("R_PULLDOWN", lockSens, 0, 10e3);

        const auto r = run(c, 0.2, 10e-6);
        check(r.ok && seq->state == DoorAutomationSequencer::State::Aborted,
              "cancellation command: aborted mid-cycle and de-energized outputs");
        out.push_back((double)seq->state);
    }

    // Scenario 5: Command preemption (new command arriving before completion at t = 0.04 s)
    {
        Circuit c;
        const auto posNode = c.addNode(), lockSens = c.addNode(), motorDrive = c.addNode(), lockDrive = c.addNode();
        auto seq = std::make_shared<DoorAutomationSequencer>();
        seq->preemptAt = 0.04;
        c.addProgrammable("SEQ", { posNode, lockSens, motorDrive, lockDrive }, seq);

        mech::DcMotorModel m;
        mech::MotorLoad load;
        load.damping = 2e-3;
        mech::ShaftModel s;
        s.theta0 = 1.0;
        s.hasMin = true; s.minAngle = 0.0;
        const auto motor = mech::addDcMotor(c, "M_DOOR", motorDrive, 0, m, load, s);
        mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);
        c.addResistor("R_PULLDOWN", lockSens, 0, 10e3);

        const auto r = run(c, 0.15, 10e-6);
        check(r.ok && seq->state == DoorAutomationSequencer::State::Idle,
              "command preemption: active closing sequence preempted by new command");
        out.push_back((double)seq->state);
    }
}

// ---- 12. failures -------------------------------------------------------------------------------------------------

void testFailures()
{
    std::printf("-- failures --\n");
    mech::DcMotorModel bad;
    bad.j = 0.0;
    bad.ra = -1.0;
    const auto p = mech::problemsWith(bad, {});
    check(p.find("inertia") != std::string::npos && p.find("resistance") != std::string::npos, "invalid motor parameters are named: " + p);
    mech::ShaftModel s;
    s.hasMin = s.hasMax = true;
    s.minAngle = 1.0;
    s.maxAngle = -1.0;
    s.smoothing = 0.0;
    const auto ps = mech::problemsWith(s);
    check(ps.find("minimum angle") != std::string::npos && ps.find("smoothing") != std::string::npos, "invalid shaft settings are named: " + ps);

    // Newton allowed 1 iteration: the run fails and says so; no samples.
    const Params pr;
    mech::ShaftModel f;
    f.coulomb = 1e-3;
    auto b = motorOnSource(pr, dc(12.0), dc(0.0), f);
    Options o;
    o.maxIterations = 1;
    const auto r = run(b.c, 0.01, 10e-6, o);
    check(!r.ok && !r.error.empty(), "a run that cannot converge fails with an error, not a result: " + r.error);
}
} // namespace

int compareDirs(const std::string& a, const std::string& b)
{
    std::printf("-- optimised vs unoptimised (%s vs %s) --\n", a.c_str(), b.c_str());
    for (const char* name : { "startup", "load", "friction", "events", "rollback", "control" })
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

// Bounded manufacturer-profile checks, deliberately separate from the long
// motor/controller/optimisation campaign below. No transient settle loop.
int pololu25dChecks()
{
    std::ofstream rows(fs::path(outDir)/"pololu25d-benchmarks.csv");
    rows<<"part,load_fraction,expected_rpm,calculated_rpm,expected_A,calculated_A\n";
    for(const auto& r:pololu25d::references())
    {
        const auto model=pololu25d::endpointModel(r);
        check(model.ke>=model.kt&&model.ra>0,"Pololu "+std::string(r.part)+" effective conversion is passive");
        for(double fraction:{0.0,0.5,1.0})
        {
            Circuit c;const auto supply=c.addNode();Waveform voltage;voltage.offset=12;c.addVoltageSource("12V",supply,0,voltage);
            mech::MotorLoad load;load.torque.offset=fraction*r.stallKgCm*pololu25d::kgCmToNm;
            auto motor=mech::addDcMotor(c,"M",supply,0,model,load,{});const auto op=solveOperatingPoint(c);
            const double expectedRpm=r.rpm*(1-fraction),expectedI=r.noLoadCurrent+(r.stallCurrent-r.noLoadCurrent)*fraction;
            const double actualRpm=op.ok?op.voltages[(size_t)motor.shaft]*60/(2*3.14159265358979323846):NAN;
            const double actualI=op.ok?op.sourceCurrents[(size_t)motor.armature]:NAN;
            check(op.ok&&std::abs(actualRpm-expectedRpm)<1e-4&&std::abs(actualI-expectedI)<1e-6,"Pololu "+std::string(r.part)+" published typical endpoint/load line",fmt("load %.2f rpm %.9g/%.9g A %.9g/%.9g %s",fraction,actualRpm,expectedRpm,actualI,expectedI,op.error.c_str()));
            std::printf("  %s fraction=%.2f rpm expected=%.9g calculated=%.9g; A expected=%.9g calculated=%.9g\n",r.part,fraction,expectedRpm,actualRpm,expectedI,actualI);
            rows<<r.part<<","<<fraction<<","<<expectedRpm<<","<<actualRpm<<","<<expectedI<<","<<actualI<<"\n";
        }
        Circuit fitted;const auto supply=fitted.addNode();Waveform voltage;voltage.offset=12;fitted.addVoltageSource("12V",supply,0,voltage);
        mech::MotorLoad load;load.torque.offset=.2;const double kgMm=.2/(pololu25d::kgCmToNm/10);
        const auto motor=mech::addDcMotor(fitted,"M",supply,0,pololu25d::curveFitModel(r),load,{});const auto op=solveOperatingPoint(fitted);
        const double expectedRpm=r.rpm-r.fitSpeedSlope*kgMm,expectedI=r.fitCurrentIntercept+r.fitCurrentSlope*kgMm;
        check(op.ok&&std::abs(op.voltages[motor.shaft]*60/(2*3.14159265358979323846)-expectedRpm)<1e-4&&std::abs(op.sourceCurrents[motor.armature]-expectedI)<1e-6,"Pololu "+std::string(r.part)+" manufacturer fitted curves at 0.2 N*m");
    }
    check(pololu25d::references()[0].noLoadCurrent!=pololu25d::references()[3].noLoadCurrent,"MP encoder and non-encoder typical currents remain distinct");
    check(!pololu25d::references()[3].encoder,"3229 has no encoder configuration");
    pololu25d::QuadratureEncoder encoder(pololu25d::encoderRatio,100);
    std::vector<double> v(5),i;std::string error;v[1]=5;int transitions=0,previous=-1;bool gray=true,ok=true;
    for(int k=0;k<=48;++k)
    {
        v[0]=(k+.25)*3.14159265358979323846/(24*pololu25d::encoderRatio);
        ok=ok&&encoder.evaluate(0,0,v,i,error);if(!ok)break;
        const int state=(i[3]>0?1:0)|(i[4]>0?2:0);
        if(previous>=0){const int difference=state^previous;gray=gray&&(difference==1||difference==2);transitions+=difference==1||difference==2;}previous=state;
    }
    check(ok&&gray&&transitions==48,"Encoder emits 48 Gray-code edges per motor-shaft revolution");
    check(std::abs(pololu25d::encoderRatio*48-2248.86)<1e-9,"Encoder preserves published output counts rather than rounded gear ratio");
    std::vector<std::uint8_t> state;check(encoder.saveState(state)&&encoder.restoreState(state),"Encoder supports solver rollback");
    v[1]=0;encoder.evaluate(0,0,v,i,error);check(i==std::vector<double>(5,0),"Unpowered encoder is high impedance");
    v[1]=21;check(!encoder.evaluate(0,0,v,i,error)&&!error.empty(),"Encoder overvoltage is rejected");
    std::printf("Pololu 25D: %d passed, %d failed\n",passes,failures);return failures?1:0;
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::string(argv[1]) == "--compare")
        return compareDirs(argv[2], argv[3]);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--out")
            outDir = argv[i + 1];
    fs::create_directories(outDir);
    if(argc>=2&&std::string(argv[1])=="--pololu25d")return pololu25dChecks();
    std::printf("motor tests, circuit_sim configuration: %s, output: %s\n", MOTOR_TESTS_CONFIG, outDir.c_str());
    std::vector<double> startup, load, friction, events, rollback, control;
    testStartupAndSteadyState(startup);
    testHighSpeedOperatingPoint(startup);
    testLoadReversalGenerating(load);
    testInertiaAndFriction(friction);
    testSwitchingAndEvents(events);
    testRollback(rollback);
    testController(control);
    testClosedLoopSpeedControl(control);
    testClosedLoopPositionControl(control);
    testCoordinatedDoorAutomation(control);
    testFailures();
    writeValues("startup", startup);
    writeValues("load", load);
    writeValues("friction", friction);
    writeValues("events", events);
    writeValues("rollback", rollback);
    writeValues("control", control);
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}

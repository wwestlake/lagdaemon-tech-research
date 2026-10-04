// Circuit solver checks against hand-calculated results.
// Each expected value is worked out on paper first (see the comments).

#include "../../Source/CircuitSolver.h"

#include <cmath>
#include <crtdbg.h>
#include <cstdlib>
#include <cstdio>
#include <string>

using namespace circuit_sim;

namespace
{
int failures = 0;

void check(const std::string& name, double actual, double expected, double tolerance)
{
    const auto ok = std::abs(actual - expected) <= tolerance;
    std::printf("%s  %-48s actual %-12.6g expected %-12.6g (+/- %g)\n", ok ? "PASS" : "FAIL", name.c_str(), actual, expected, tolerance);
    if (!ok) ++failures;
}

void checkTrue(const std::string& name, bool condition, const std::string& detail = {})
{
    std::printf("%s  %s %s\n", condition ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (!condition) ++failures;
}

Waveform dc(double v) { Waveform w; w.offset = v; return w; }
Waveform sine(double amp, double freq) { Waveform w; w.kind = Waveform::Kind::Sine; w.amplitude = amp; w.frequency = freq; w.acMagnitude = 1.0; return w; }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Headless: assertion failures go to stderr, never to a dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    // 1. Divider: 10 V across 1k + 1k -> 5 V at the middle.
    {
        Circuit c;
        auto top = c.addNode(), mid = c.addNode();
        c.addVoltageSource("V1", top, 0, dc(10.0));
        c.addResistor("R1", top, mid, 1000.0);
        c.addResistor("R2", mid, 0, 1000.0);
        const auto op = solveOperatingPoint(c);
        checkTrue("divider solves", op.ok, op.error);
        check("divider midpoint (V)", op.voltages[(size_t)mid], 5.0, 1e-6);
        // Source current: 10 V / 2k = 5 mA, flowing out of + through the circuit,
        // i.e. -5 mA through the source from + to -.
        check("divider source current (A)", op.sourceCurrents[0], -0.005, 1e-8);
    }

    // 2. RC low-pass 1k / 1u: fc = 1/(2 pi R C) = 159.155 Hz, |H(fc)| = 0.70711.
    //    At 1 kHz: |H| = 1/sqrt(1 + (1000/159.155)^2) = 0.15718.
    {
        Circuit c;
        auto in = c.addNode(), out = c.addNode();
        c.addVoltageSource("V1", in, 0, sine(1.0, 1000.0));
        c.addResistor("R1", in, out, 1000.0);
        c.addCapacitor("C1", out, 0, 1e-6);
        const auto ac = solveAc(c, 159.155, 159.156, 1);
        checkTrue("RC AC solves", ac.ok, ac.error);
        check("RC |H| at fc", std::abs(ac.voltages[0][(size_t)out]), 0.70711, 1e-3);

        // Transient: tau = 1 ms; after 10 ms the start-up has decayed.
        const auto tr = solveTransient(c, 0.010, 1e-6);
        checkTrue("RC transient solves", tr.ok, tr.error);
        double peak = 0.0;
        for (size_t i = 0; i < tr.time.size(); ++i)
            if (tr.time[i] > 0.008)
                peak = std::max(peak, tr.voltages[i][(size_t)out]);
        check("RC steady peak at 1 kHz (V)", peak, 0.15718, 2e-3);
    }

    // 3. Diode: 5 V, 1k, Is = 1e-14, n = 1.
    //    Id = (5 - Vd)/1k, Vd = Vt ln(Id/Is) -> Vd = 0.6927 V, Id = 4.307 mA.
    {
        Circuit c;
        auto a = c.addNode(), k = c.addNode();
        c.addVoltageSource("V1", a, 0, dc(5.0));
        c.addResistor("R1", a, k, 1000.0);
        c.addDiode("D1", k, 0);
        const auto op = solveOperatingPoint(c);
        checkTrue("diode solves", op.ok, op.error);
        check("diode forward drop (V)", op.voltages[(size_t)k], 0.6927, 2e-3);
    }

    // 4. Common-emitter NPN, beta 100: Vcc 12, RB 1M to base, RC 2k.
    //    Vbe = 0.658, Ib = (12 - 0.658)/1M = 11.34 uA, Ic = 1.134 mA,
    //    Vc = 12 - 2k * 1.134 mA = 9.731 V.
    {
        Circuit c;
        auto vcc = c.addNode(), b = c.addNode(), col = c.addNode();
        c.addVoltageSource("VCC", vcc, 0, dc(12.0));
        c.addResistor("RB", vcc, b, 1e6);
        c.addResistor("RC", vcc, col, 2000.0);
        c.addBjt("Q1", true, col, b, 0);
        const auto op = solveOperatingPoint(c);
        checkTrue("BJT solves", op.ok, op.error);
        check("BJT base (V)", op.voltages[(size_t)b], 0.658, 5e-3);
        check("BJT collector (V)", op.voltages[(size_t)col], 9.731, 0.02);
    }

    // 5. Inverting op amp: Vin 0.1 V DC, R1 10k, R2 100k, rails +/-15 -> Vout = -1.000 V.
    //    AC: gain magnitude 10.
    {
        Circuit c;
        auto in = c.addNode(), inv = c.addNode(), out = c.addNode(), vp = c.addNode(), vm = c.addNode();
        Waveform w = dc(0.1);
        w.acMagnitude = 1.0;
        c.addVoltageSource("VIN", in, 0, w);
        c.addVoltageSource("VP", vp, 0, dc(15.0));
        c.addVoltageSource("VM", 0, vm, dc(15.0));
        c.addResistor("R1", in, inv, 10e3);
        c.addResistor("R2", inv, out, 100e3);
        c.addOpAmp("U1", 0, inv, out, vp, vm);
        const auto op = solveOperatingPoint(c);
        checkTrue("op amp solves", op.ok, op.error);
        check("op amp output (V)", op.voltages[(size_t)out], -1.0, 2e-3);
        const auto ac = solveAc(c, 1000.0, 1001.0, 1);
        checkTrue("op amp AC solves", ac.ok, ac.error);
        check("op amp |gain| at 1 kHz", std::abs(ac.voltages[0][(size_t)out]), 10.0, 0.01);
        check("op amp minus rail (V)", op.voltages[(size_t)vm], -15.0, 1e-6);
    }

    // 6. NMOS: Vg = 3 V, Vt = 1, K = 2 mA/V^2, lambda 0, RD 1k to 10 V.
    //    Saturation: Id = K/2 (Vgs - Vt)^2 = 4 mA, Vd = 10 - 4 = 6 V (> Vov = 2, so saturated).
    {
        Circuit c;
        auto vdd = c.addNode(), g = c.addNode(), d = c.addNode();
        c.addVoltageSource("VDD", vdd, 0, dc(10.0));
        c.addVoltageSource("VG", g, 0, dc(3.0));
        c.addResistor("RD", vdd, d, 1000.0);
        MosModel m;
        m.threshold = 1.0;
        m.transconductance = 2e-3;
        m.lambda = 0.0;
        c.addMosfet("M1", true, d, g, 0, m);
        const auto op = solveOperatingPoint(c);
        checkTrue("NMOS solves", op.ok, op.error);
        check("NMOS drain (V)", op.voltages[(size_t)d], 6.0, 1e-3);
    }

    // 7. RL low-pass: L 10 mH in series, R 100 to ground: fc = R/(2 pi L) = 1591.55 Hz.
    {
        Circuit c;
        auto in = c.addNode(), out = c.addNode();
        c.addVoltageSource("V1", in, 0, sine(1.0, 1000.0));
        c.addInductor("L1", in, out, 10e-3);
        c.addResistor("R1", out, 0, 100.0);
        const auto ac = solveAc(c, 1591.55, 1591.56, 1);
        checkTrue("RL AC solves", ac.ok, ac.error);
        check("RL |H| at fc", std::abs(ac.voltages[0][(size_t)out]), 0.70711, 1e-3);
    }

    // 8. Transformer 1:1, L = 1 H each, k = 0.999, 1k load, 1 kHz: |V2/V1| ~ 1 (> 0.98).
    {
        Circuit c;
        // A real source has some resistance; a bare voltage source across an
        // inductor has no DC solution (the inductor is a short at DC).
        auto src = c.addNode(), p = c.addNode(), s = c.addNode();
        c.addVoltageSource("V1", src, 0, sine(1.0, 1000.0));
        c.addResistor("RS", src, p, 1.0);
        const auto l1 = c.addInductor("LP", p, 0, 1.0);
        const auto l2 = c.addInductor("LS", s, 0, 1.0);
        c.addCoupling("K1", l1, l2, 0.999);
        c.addResistor("RL", s, 0, 1000.0);
        const auto ac = solveAc(c, 1000.0, 1001.0, 1);
        checkTrue("transformer AC solves", ac.ok, ac.error);
        const auto ratio = ac.ok ? std::abs(ac.voltages[0][(size_t)s] / ac.voltages[0][(size_t)p]) : 0.0;
        checkTrue("transformer |Vs/Vp| near 1", ratio > 0.98 && ratio < 1.02, "(" + std::to_string(ratio) + ")");
    }

    // 8b. A voltage source shorted by an inductor has no DC solution: the
    //     solver must say so instead of failing obscurely.
    {
        Circuit c;
        auto p = c.addNode();
        c.addVoltageSource("V1", p, 0, dc(1.0));
        c.addInductor("L1", p, 0, 1e-3);
        const auto op = solveOperatingPoint(c);
        checkTrue("shorted source reported as singular", !op.ok && op.error.find("Singular") != std::string::npos, "(" + op.error + ")");
    }

    // 9. Value parsing.
    {
        double v = 0.0;
        checkTrue("parse 4.7k", parseValue("4.7k", v) && std::abs(v - 4700.0) < 1e-9);
        checkTrue("parse 4k7", parseValue("4k7", v) && std::abs(v - 4700.0) < 1e-9);
        checkTrue("parse 10uF", parseValue("10uF", v) && std::abs(v - 1e-5) < 1e-18);
        checkTrue("parse 1meg", parseValue("1meg", v) && std::abs(v - 1e6) < 1e-6);
        checkTrue("parse 3M (mega)", parseValue("3M", v) && std::abs(v - 3e6) < 1e-6);
        checkTrue("parse 5m (milli)", parseValue("5m", v) && std::abs(v - 5e-3) < 1e-15);
        checkTrue("parse 100p", parseValue("100p", v) && std::abs(v - 1e-10) < 1e-22);
        checkTrue("parse 12V", parseValue("12V", v) && std::abs(v - 12.0) < 1e-12);
        checkTrue("reject abc", !parseValue("abc", v));
        checkTrue("reject 1x2", !parseValue("1x2", v));
        checkTrue("format 4700 ohm", formatValue(4700.0, "ohm") == "4.7 kohm", "(" + formatValue(4700.0, "ohm") + ")");
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

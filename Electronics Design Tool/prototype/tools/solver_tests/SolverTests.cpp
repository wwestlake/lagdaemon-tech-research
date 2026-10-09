// Circuit solver checks against hand-calculated results.
// Each expected value is worked out on paper first (see the comments).

#include "../../Source/CircuitSolver.h"
#include "../../Source/SignalMeasure.h"
#include "../../Source/CircuitHierarchy.h"

#include <cmath>
#include <crtdbg.h>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

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
        std::map<std::string, double> params;
        std::string error;
        checkTrue("resolve simple parameter", resolveParameters({ { "RBASE", "1000" } }, params, error) && std::abs(params["RBASE"] - 1000.0) < 1e-9, error);
        checkTrue("resolve derived parameter", resolveParameters({ { "SCALE", "2" }, { "RBASE", "1k" }, { "R2", "RBASE*SCALE" } }, params, error)
                  && std::abs(params["R2"] - 2000.0) < 1e-9, error);
        {
            // Component value fields resolve through evaluateExpression once a
            // plain number fails; labels must never fall back to a default.
            std::map<std::string, double> circuitParams;
            std::string valueError;
            checkTrue("component params resolve", resolveParameters({ { "RLOAD", "2.2k" } }, circuitParams, valueError), valueError);
            double cv = 0.0;
            valueError.clear();
            checkTrue("component value 4.7k", evaluateExpression("4.7k", circuitParams, cv, valueError) && std::abs(cv - 4700.0) < 1e-9, valueError);
            valueError.clear();
            checkTrue("component value {RLOAD}", evaluateExpression("{RLOAD}", circuitParams, cv, valueError) && std::abs(cv - 2200.0) < 1e-9, valueError);
            valueError.clear();
            checkTrue("component value RD rejected", !evaluateExpression("RD", circuitParams, cv, valueError)
                      && valueError == "undefined parameter 'RD'", valueError);
            valueError.clear();
            checkTrue("component value {UNDEFINED} rejected", !evaluateExpression("{UNDEFINED}", circuitParams, cv, valueError)
                      && valueError == "undefined parameter 'UNDEFINED'", valueError);
            valueError.clear();
            checkTrue("capacitor value CIN rejected", !evaluateExpression("CIN", circuitParams, cv, valueError)
                      && valueError == "undefined parameter 'CIN'", valueError);
        }
        {
            Circuit c;
            auto n = c.addNode();
            c.addVoltageSource("V1", n, 0, dc(10.0));
            const auto r = c.addResistor("RLOAD", n, 0, 1.0);
            c.elements()[(size_t)r].valueExpression = "RBASE*SCALE";
            for (const auto scale : { 1.0, 2.0, 5.0 })
            {
                const std::vector<std::pair<std::string, std::string>> defs = { { "RBASE", "1k" }, { "SCALE", std::to_string(scale) } };
                checkTrue("resolve sweep point", resolveParameters(defs, params, error), error);
                checkTrue("apply sweep point", applyParameterValues(c, params, error), error);
                const auto op = solveOperatingPoint(c);
                checkTrue("parameter sweep point solves", op.ok, op.error);
                check("parameter sweep source current", op.sourceCurrents[0], -10.0 / (1000.0 * scale), 1e-9);
            }
            checkTrue("sweep keeps expression text", c.elements()[(size_t)r].valueExpression == "RBASE*SCALE");
        }
        checkTrue("reject undefined parameter", !resolveParameters({ { "R1", "MISSING" } }, params, error), error);
        checkTrue("reject circular parameter", !resolveParameters({ { "A", "B" }, { "B", "A" } }, params, error), error);
        checkTrue("reject divide by zero", !resolveParameters({ { "BAD", "1/0" } }, params, error), error);
        checkTrue("reject unsupported function", !resolveParameters({ { "BAD", "sin(1)" } }, params, error), error);
        checkTrue("format 4700 ohm", formatValue(4700.0, "ohm") == "4.7 kohm", "(" + formatValue(4700.0, "ohm") + ")");
    }

    // ---- SPICE analytics -------------------------------------------------------

    // 10. DC sweep: V1 0..10 V into 1k / R2. Vmid = V1 * R2 / (1k + R2).
    //     V1 = 4 V, R2 = 1k -> 2 V; V1 = 8 V, R2 = 3k (outer step) -> 6 V.
    {
        Circuit c;
        auto top = c.addNode(), mid = c.addNode();
        const auto v1 = c.addVoltageSource("V1", top, 0, dc(0.0));
        c.addResistor("R1", top, mid, 1000.0);
        const auto r2 = c.addResistor("R2", mid, 0, 1000.0);
        SweepAxis inner, outer;
        inner.element = v1; inner.parameter = "dc";
        for (int i = 0; i <= 10; ++i) inner.values.push_back(i);
        outer.element = r2; outer.parameter = "value"; outer.values = { 1000.0, 3000.0 };
        const auto sw = solveDcSweep(c, inner, outer);
        checkTrue("DC sweep solves", sw.ok, sw.error);
        if (sw.ok)
        {
            check("DC sweep V1=4, R2=1k -> Vmid (V)", sw.points[0][4].voltages[(size_t)mid], 2.0, 1e-6);
            check("DC sweep V1=8, R2=3k -> Vmid (V)", sw.points[1][8].voltages[(size_t)mid], 6.0, 1e-6);
        }
    }

    // 11. Transfer function of the 1k/1k divider from V1 to mid:
    //     gain 0.5, Rin = 2k, Rout = 1k || 1k = 500 ohm.
    //     Current source into 2k: transimpedance 2000 ohm, Rin 2k, Rout 2k.
    {
        Circuit c;
        auto top = c.addNode(), mid = c.addNode();
        const auto v1 = c.addVoltageSource("V1", top, 0, dc(10.0));
        c.addResistor("R1", top, mid, 1000.0);
        c.addResistor("R2", mid, 0, 1000.0);
        const auto tf = solveTransferFunction(c, mid, 0, v1);
        checkTrue("TF solves", tf.ok, tf.error);
        check("TF gain (V/V)", tf.gain, 0.5, 1e-9);
        check("TF input resistance (ohm)", tf.inputResistance, 2000.0, 1e-3);
        check("TF output resistance (ohm)", tf.outputResistance, 500.0, 1e-3);

        Circuit d;
        auto n = d.addNode();
        Waveform w; w.offset = 1e-3;
        const auto i1 = d.addCurrentSource("I1", 0, n, w); // pushes current into n
        d.addResistor("R", n, 0, 2000.0);
        const auto ti = solveTransferFunction(d, n, 0, i1);
        check("TF transimpedance (ohm)", ti.gain, 2000.0, 1e-3);
        check("TF current-input Rin (ohm)", ti.inputResistance, 2000.0, 1e-3);
    }

    // 12. Noise.
    //  a) 1k/1k divider: output sees 500 ohm -> sqrt(4kT*500) = 2.87889e-9 V/rtHz at 300.15 K,
    //     input-referred x2 = 5.75779e-9; flat, so 10 Hz..100 kHz integrates to 9.10341e-7 V rms.
    //  b) 1k || 1nF: integrated over all frequencies sqrt(kT/C) = 2.03569e-6 V rms (1 Hz..1 GHz here).
    //  c) Diode forced to 1 mA: shot current sqrt(2qI) = 1.79007e-11 A/rtHz through rd = 25.852 ohm
    //     -> 4.62769e-10 V/rtHz at the diode; input-referred (current input) = 1.79007e-11 A/rtHz.
    {
        Circuit c;
        auto top = c.addNode(), mid = c.addNode();
        const auto v1 = c.addVoltageSource("V1", top, 0, dc(1.0));
        c.addResistor("R1", top, mid, 1000.0);
        c.addResistor("R2", mid, 0, 1000.0);
        const auto nr = solveNoise(c, mid, 0, v1, 10.0, 100e3, 10);
        checkTrue("noise (divider) solves", nr.ok, nr.error);
        if (nr.ok)
        {
            check("noise output density (V/rtHz)", nr.outputDensity[3], 2.87889e-9, 2e-13);
            check("noise input-referred density (V/rtHz)", nr.inputDensity[3], 5.75779e-9, 4e-13);
            check("noise integrated output (V rms)", nr.integratedOutputRms, 9.10341e-7, 2e-10);
            check("noise contributions: two resistors", (double)nr.contributions.size(), 2.0, 0.0);
        }

        Circuit k;
        auto n = k.addNode();
        Waveform w; w.offset = 0.0;
        const auto i1 = k.addCurrentSource("I1", 0, n, w);
        k.addResistor("R", n, 0, 1000.0);
        k.addCapacitor("C", n, 0, 1e-9);
        const auto kn = solveNoise(k, n, 0, i1, 1.0, 1e9, 50);
        checkTrue("noise (kT/C) solves", kn.ok, kn.error);
        check("noise kT/C integrated (V rms)", kn.integratedOutputRms, 2.03569e-6, 2.03569e-6 * 0.01);

        Circuit s;
        auto a = s.addNode();
        Waveform id; id.offset = 1e-3;
        const auto src = s.addCurrentSource("I1", 0, a, id);
        s.addDiode("D1", a, 0);
        const auto sn = solveNoise(s, a, 0, src, 100.0, 1000.0, 1);
        checkTrue("noise (shot) solves", sn.ok, sn.error);
        if (sn.ok)
        {
            check("shot noise voltage at diode (V/rtHz)", sn.outputDensity[0], 4.62769e-10, 1e-13);
            check("shot noise input-referred (A/rtHz)", sn.inputDensity[0], 1.79007e-11, 5e-15);
        }
    }

    // 13. Sensitivity of the 10 V, 1k/1k divider midpoint:
    //     dV/dR2 = V R1/(R1+R2)^2 = 2.5e-3 V/ohm -> +0.025 V per +1 %; R1 -> -0.025 V; V1 -> +0.05 V.
    //     AC: RC low-pass at fc, d(dB)/d(ln R) = -(20/ln 10)/2 -> -0.0434294 dB per +1 % of R (and of C).
    {
        Circuit c;
        auto top = c.addNode(), mid = c.addNode();
        c.addVoltageSource("V1", top, 0, dc(10.0));
        c.addResistor("R1", top, mid, 1000.0);
        c.addResistor("R2", mid, 0, 1000.0);
        const auto sr = solveDcSensitivity(c, mid, 0);
        checkTrue("DC sensitivity solves", sr.ok, sr.error);
        auto find = [&](const SensitivityResult& r, const std::string& e, const std::string& p) {
            for (const auto& item : r.items) if (item.element == e && item.parameter == p) return item;
            return SensitivityItem {};
        };
        check("sens R2 d/dR (V/ohm)", find(sr, "R2", "value").absolute, 2.5e-3, 1e-6);
        check("sens R2 per 1 % (V)", find(sr, "R2", "value").normalized, 0.025, 1e-5);
        check("sens R1 per 1 % (V)", find(sr, "R1", "value").normalized, -0.025, 1e-5);
        check("sens V1 per 1 % (V)", find(sr, "V1", "dc").normalized, 0.05, 1e-5);

        Circuit lp;
        auto in = lp.addNode(), out = lp.addNode();
        const auto v = lp.addVoltageSource("V1", in, 0, sine(1.0, 1000.0));
        lp.addResistor("R", in, out, 1000.0);
        lp.addCapacitor("C", out, 0, 1e-6);
        const auto as = solveAcSensitivity(lp, out, 0, v, 1.0 / (2.0 * 3.14159265358979 * 1e-3));
        checkTrue("AC sensitivity solves", as.ok, as.error);
        check("AC sens R per 1 % (dB)", find(as, "R", "value").normalized, -0.0434294, 2e-5);
        check("AC sens C per 1 % (dB)", find(as, "C", "value").normalized, -0.0434294, 2e-5);
        check("AC sens nominal gain (dB)", as.output, -3.0103, 1e-3);
    }

    // 14. Poles and zeros.
    //  RC low-pass 1k/1u: one pole at -1000 rad/s, no finite zero.
    //  RC high-pass: pole at -1000 rad/s, zero at 0.
    //  Series RLC (100 ohm, 10 mH, 1 uF), output across C: poles -5000 +/- j8660.25 rad/s.
    {
        Circuit lp;
        auto in = lp.addNode(), out = lp.addNode();
        const auto v = lp.addVoltageSource("V1", in, 0, dc(0.0));
        lp.addResistor("R", in, out, 1000.0);
        lp.addCapacitor("C", out, 0, 1e-6);
        const auto pz = solvePoleZero(lp, out, 0, v);
        checkTrue("PZ low-pass solves", pz.ok, pz.error);
        checkTrue("PZ low-pass has 1 pole, 0 zeros", pz.poles.size() == 1 && pz.zeros.empty(),
                  "(" + std::to_string(pz.poles.size()) + " poles, " + std::to_string(pz.zeros.size()) + " zeros)");
        if (!pz.poles.empty()) check("PZ low-pass pole (rad/s)", pz.poles[0].real(), -1000.0, 1e-3);
        check("PZ low-pass DC gain", pz.dcGain, 1.0, 1e-9);

        Circuit hp;
        auto hin = hp.addNode(), hout = hp.addNode();
        const auto hv = hp.addVoltageSource("V1", hin, 0, dc(0.0));
        hp.addCapacitor("C", hin, hout, 1e-6);
        hp.addResistor("R", hout, 0, 1000.0);
        const auto hz = solvePoleZero(hp, hout, 0, hv);
        checkTrue("PZ high-pass solves", hz.ok, hz.error);
        checkTrue("PZ high-pass has 1 pole, 1 zero", hz.poles.size() == 1 && hz.zeros.size() == 1,
                  "(" + std::to_string(hz.poles.size()) + " poles, " + std::to_string(hz.zeros.size()) + " zeros)");
        if (!hz.poles.empty()) check("PZ high-pass pole (rad/s)", hz.poles[0].real(), -1000.0, 1e-3);
        if (!hz.zeros.empty()) check("PZ high-pass zero |s| (rad/s)", std::abs(hz.zeros[0]), 0.0, 1e-3);

        Circuit rlc;
        auto a = rlc.addNode(), b = rlc.addNode(), o = rlc.addNode();
        const auto rv = rlc.addVoltageSource("V1", a, 0, dc(0.0));
        rlc.addResistor("R", a, b, 100.0);
        rlc.addInductor("L", b, o, 10e-3);
        rlc.addCapacitor("C", o, 0, 1e-6);
        const auto rz = solvePoleZero(rlc, o, 0, rv);
        checkTrue("PZ RLC solves", rz.ok, rz.error);
        checkTrue("PZ RLC has 2 poles", rz.poles.size() == 2, "(" + std::to_string(rz.poles.size()) + ")");
        if (rz.poles.size() == 2)
        {
            check("PZ RLC pole real (rad/s)", rz.poles[0].real(), -5000.0, 1e-2);
            check("PZ RLC pole |imag| (rad/s)", std::abs(rz.poles[0].imag()), 8660.254, 1e-2);
            check("PZ RLC conjugate pair", rz.poles[0].imag() + rz.poles[1].imag(), 0.0, 1e-6);
        }
    }

    // 15. Fourier: v = sin(wt) + 0.1 sin(3wt) at 1 kHz -> H1 = 1, H3 = 0.1, THD = 10 %.
    //     A pure 1 V, 1 kHz sine through a resistor: THD ~ 0.
    {
        std::vector<double> t, v;
        for (int k = 0; k <= 4000; ++k)
        {
            const auto time = k * 1e-6;
            t.push_back(time);
            v.push_back(std::sin(2.0 * 3.14159265358979 * 1000.0 * time) + 0.1 * std::sin(2.0 * 3.14159265358979 * 3000.0 * time));
        }
        const auto fr = fourier(t, v, 1000.0, 9, 2);
        checkTrue("Fourier solves", fr.ok, fr.error);
        if (fr.ok)
        {
            check("Fourier H1 (V)", fr.magnitude[0], 1.0, 1e-4);
            check("Fourier H3 (V)", fr.magnitude[2], 0.1, 1e-4);
            check("Fourier H2 (V)", fr.magnitude[1], 0.0, 1e-4);
            check("Fourier THD (%)", fr.thdPercent, 10.0, 1e-2);
        }
        Circuit c;
        auto n = c.addNode();
        c.addVoltageSource("V1", n, 0, sine(1.0, 1000.0));
        c.addResistor("R", n, 0, 1000.0);
        const auto tr = solveTransient(c, 5e-3, 1e-6, {}, 1 << 20);
        std::vector<double> vn;
        for (const auto& s : tr.voltages) vn.push_back(s[(size_t)n]);
        const auto fs = fourier(tr.time, vn, 1000.0, 9, 1);
        check("Fourier of a source sine: THD (%)", fs.thdPercent, 0.0, 1e-3);
        check("Fourier of a source sine: H1 (V)", fs.magnitude.empty() ? 0.0 : fs.magnitude[0], 1.0, 1e-4);
    }

    // 16. Pulse and PWL sources.
    //     RC 1k/1u driven by a 0 -> 1 V pulse at 1 ms (1 us edges): one time constant later
    //     (t = 2 ms + half the edge) v = 1 - e^-1 = 0.632121.
    {
        Circuit c;
        auto in = c.addNode(), out = c.addNode();
        Waveform p;
        p.kind = Waveform::Kind::Pulse;
        p.offset = 0.0; p.pulsed = 1.0; p.delay = 1e-3; p.rise = 1e-6; p.fall = 1e-6; p.width = 10e-3; p.period = 0.0;
        c.addVoltageSource("V1", in, 0, p);
        c.addResistor("R", in, out, 1000.0);
        c.addCapacitor("C", out, 0, 1e-6);
        TransientSettings ts;
        ts.stop = 4e-3; ts.step = 1e-5; ts.maxSamples = 1 << 20;
        const auto tr = solveTransient(c, ts);
        checkTrue("pulse transient solves", tr.ok, tr.error);
        // Compare at the first stored sample past one tau with the exact 1 - e^-(t - 1.0005 ms)/tau there.
        double at = 0.0, when = 0.0;
        for (size_t s = 0; s < tr.time.size(); ++s)
            if (tr.time[s] >= 2.0005e-3) { at = tr.voltages[s][(size_t)out]; when = tr.time[s]; break; }
        check("RC pulse response near one tau (V)", at, 1.0 - std::exp(-(when - 1.0005e-3) / 1e-3), 2e-4);
        bool cornerHit = false;
        for (auto time : tr.time) if (std::abs(time - 1.001e-3) < 1e-12) cornerHit = true;
        checkTrue("pulse corner at 1.001 ms is a time point", cornerHit);

        Waveform w;
        w.kind = Waveform::Kind::Pwl;
        w.points = { { 0.0, 0.0 }, { 1e-3, 5.0 }, { 3e-3, 5.0 }, { 4e-3, -1.0 } };
        check("PWL at 0.5 ms (V)", w.valueAt(0.5e-3), 2.5, 1e-12);
        check("PWL at 3.5 ms (V)", w.valueAt(3.5e-3), 2.0, 1e-12);
        check("PWL after the last point (V)", w.valueAt(9e-3), -1.0, 1e-12);
    }

    // 17. Temperature: diode forced to 1 mA (Is = 1e-14 at 27 C, n = 1).
    //     27 C: Vd = Vt ln(I/Is + 1) = 0.654791 V.
    //     127 C: Vt = 34.4823 mV, Is = Is0 (T/Tn)^3 exp(Eg/Vt (T/Tn - 1)) = 1.07739e-9 A -> Vd = 0.473820 V.
    {
        Circuit c;
        auto a = c.addNode();
        Waveform id; id.offset = 1e-3;
        c.addCurrentSource("I1", 0, a, id);
        c.addDiode("D1", a, 0);
        const auto cold = solveOperatingPoint(c);
        const auto hot = solveOperatingPoint(atTemperature(c, 127.0));
        check("diode Vd at 27 C (V)", cold.voltages[(size_t)a], 0.654791, 1e-5);
        check("diode Vd at 127 C (V)", hot.voltages[(size_t)a], 0.473820, 1e-5);

        Circuit r;
        auto n = r.addNode();
        r.addVoltageSource("V1", n, 0, dc(1.0));
        const auto res = r.addResistor("R", n, 0, 1000.0);
        r.elements()[(size_t)res].tc1 = 0.004;
        const auto rh = atTemperature(r, 77.0);
        check("resistor tc1 = 4000 ppm/K at +50 K (ohm)", rh.elements()[(size_t)res].value, 1200.0, 1e-9);
    }

    // 18. Device detail and diffusion capacitance.
    //  NPN with Vbe = 0.65 V, no Early effect: gm = Ic / Vt.
    //  Diode at 1 mA with tt = 1 us: rd = 25.852 ohm, Cd = tt / rd, corner 1/(2 pi tt) = 159.155 kHz,
    //  |Z| there = rd / sqrt(2) = 18.2801 ohm.
    {
        Circuit c;
        auto b = c.addNode(), col = c.addNode();
        c.addVoltageSource("VB", b, 0, dc(0.65));
        c.addVoltageSource("VC", col, 0, dc(10.0));
        const auto q = c.addBjt("Q1", true, col, b, 0);
        const auto op = solveOperatingPoint(c);
        const auto info = deviceInfo(c, op, q);
        double ic = 0.0, gm = 0.0;
        for (const auto& [name, value] : info.values) { if (name == "Ic") ic = value; if (name == "gm") gm = value; }
        check("BJT gm / (Ic/Vt)", ic > 0.0 ? gm / (ic / 0.025852) : 0.0, 1.0, 1e-3);
        checkTrue("BJT region forward active", info.region == "forward active", "(" + info.region + ")");

        Circuit d;
        auto a = d.addNode();
        Waveform id; id.offset = 1e-3; id.acMagnitude = 1.0;
        d.addCurrentSource("I1", 0, a, id);
        DiodeModel m; m.transitTime = 1e-6;
        d.addDiode("D1", a, 0, m);
        const auto ac = solveAcAt(d, { 159154.943 });
        check("diode |Z| at 1/(2 pi tt) (ohm)", ac.ok ? std::abs(ac.voltages[0][(size_t)a]) : 0.0, 18.2801, 2e-3);

        // Power: 10 V across 1k + 1k -> each resistor 25 mW, the source delivers 50 mW.
        Circuit p;
        auto top = p.addNode(), mid = p.addNode();
        const auto v1 = p.addVoltageSource("V1", top, 0, dc(10.0));
        const auto r1 = p.addResistor("R1", top, mid, 1000.0);
        p.addResistor("R2", mid, 0, 1000.0);
        const auto pop = solveOperatingPoint(p);
        check("R1 power (W)", absorbedPower(p, pop, r1), 0.025, 1e-9);
        check("V1 power absorbed (W)", absorbedPower(p, pop, v1), -0.05, 1e-9);
    }

    // 19. Measurements.
    //  1 - e^-t/tau, tau = 1 ms: 10-90 % rise = tau ln 9 = 2.19722 ms.
    //  Integrator-like loop gain f0/f with -90 deg: unity gain at f0 = 1 kHz, phase margin 90 deg.
    {
        std::vector<double> t, y;
        for (int k = 0; k <= 20000; ++k) { t.push_back(k * 1e-6); y.push_back(1.0 - std::exp(-t.back() / 1e-3)); }
        signal_measure::Request rq;
        rq.kind = signal_measure::Kind::RiseTime;
        check("measure rise time (s)", signal_measure::measure(rq, t, y).value, 2.19722e-3, 2e-6);
        std::vector<double> step { 0.0, 1.2, 1.0, 1.0 }, st { 0.0, 1.0, 2.0, 3.0 };
        rq.kind = signal_measure::Kind::OvershootPercent;
        check("measure overshoot (%)", signal_measure::measure(rq, st, step).value, 20.0, 1e-9);
        std::vector<double> f, db, ph;
        for (int k = 0; k <= 60; ++k) { f.push_back(10.0 * std::pow(10.0, k / 15.0)); db.push_back(20.0 * std::log10(1000.0 / f.back())); ph.push_back(-90.0); }
        rq.kind = signal_measure::Kind::UnityGainFrequency;
        check("measure unity-gain frequency (Hz)", signal_measure::measure(rq, f, db, ph).value, 1000.0, 1e-6);
        rq.kind = signal_measure::Kind::PhaseMargin;
        check("measure phase margin (deg)", signal_measure::measure(rq, f, db, ph).value, 90.0, 1e-9);
        std::vector<double> sq;
        for (size_t k = 0; k < t.size(); ++k) sq.push_back(std::sin(2.0 * 3.14159265358979 * 1000.0 * t[k]));
        rq.kind = signal_measure::Kind::Rms;
        check("measure RMS of a 1 V sine (V)", signal_measure::measure(rq, t, sq).value, 0.707107, 1e-5);
        rq.kind = signal_measure::Kind::Frequency;
        check("measure frequency (Hz)", signal_measure::measure(rq, t, sq).value, 1000.0, 1e-3);
        std::vector<double> unevenT { 0.0, 1.0, 3.0, 6.0 };
        std::vector<double> constant { 5.0, 5.0, 5.0, 5.0 };
        rq = {};
        rq.kind = signal_measure::Kind::Maximum;
        check("measure MAX constant (V)", signal_measure::measure(rq, unevenT, constant).value, 5.0, 1e-12);
        rq.kind = signal_measure::Kind::Minimum;
        check("measure MIN constant (V)", signal_measure::measure(rq, unevenT, constant).value, 5.0, 1e-12);
        rq.kind = signal_measure::Kind::Average;
        check("measure weighted AVG constant (V)", signal_measure::measure(rq, unevenT, constant).value, 5.0, 1e-12);
        rq.kind = signal_measure::Kind::PeakToPeak;
        check("measure PP constant (V)", signal_measure::measure(rq, unevenT, constant).value, 0.0, 1e-12);
        std::vector<double> rampT { 0.0, 0.002, 0.007, 0.010 };
        std::vector<double> rampY { 0.0, 2.0, 7.0, 10.0 };
        rq.kind = signal_measure::Kind::ValueAt; rq.at = 0.005;
        check("measure FIND ramp at 5 ms (V)", signal_measure::measure(rq, rampT, rampY).value, 5.0, 1e-12);
        rq.kind = signal_measure::Kind::WhenCrosses; rq.level = 7.0; rq.edge = signal_measure::Edge::Rising; rq.nth = 1;
        check("measure WHEN ramp crosses 7 V (s)", signal_measure::measure(rq, rampT, rampY).value, 0.007, 1e-12);
        std::vector<double> multiT { 0, 1, 2, 3, 4, 5 };
        std::vector<double> multiY { 0, 2, 0, 2, 0, 2 };
        rq.level = 1.0; rq.nth = 2; rq.edge = signal_measure::Edge::Rising;
        check("measure second rising crossing (s)", signal_measure::measure(rq, multiT, multiY).value, 2.5, 1e-12);
        rq.edge = signal_measure::Edge::Falling;
        check("measure second falling crossing (s)", signal_measure::measure(rq, multiT, multiY).value, 3.5, 1e-12);
        rq.level = 3.0;
        checkTrue("measure missing crossing fails", !signal_measure::measure(rq, multiT, multiY).ok);
    }

    // 20. Convergence (junction limiting, gmin stepping, transient step halving).
    //  CE stage, 12 V, 47k/10k divider, 4.7k collector, 1k emitter, beta 100, VAF 100:
    //  Ic = 1.326 mA (the bench value), reached directly by Newton in a few dozen iterations.
    //  Diode clipper driven by 5 V peak through 1k: clips near +/-0.7 V, transient completes.
    {
        Circuit c;
        auto vcc = c.addNode(), b = c.addNode(), col = c.addNode(), em = c.addNode();
        c.addVoltageSource("VCC", vcc, 0, dc(12.0));
        c.addResistor("RB1", vcc, b, 47e3);
        c.addResistor("RB2", b, 0, 10e3);
        c.addResistor("RC", vcc, col, 4.7e3);
        c.addResistor("RE", em, 0, 1e3);
        BjtModel m; m.earlyVoltage = 100.0;
        const auto q = c.addBjt("Q1", true, col, b, em, m);
        const auto op = solveOperatingPoint(c);
        checkTrue("CE bias converges", op.ok, op.error);
        check("CE bias Ic (A)", op.ok ? terminalCurrents(c, op, q)[0] : 0.0, 1.326e-3, 2e-6);
        checkTrue("CE bias Newton iterations < 60", op.iterations < 60, "(" + std::to_string(op.iterations) + ")");

        Circuit k;
        auto in = k.addNode(), out = k.addNode();
        k.addVoltageSource("V1", in, 0, sine(5.0, 1000.0));
        k.addResistor("R", in, out, 1000.0);
        DiodeModel d; d.saturationCurrent = 2.52e-9; d.emission = 1.752;
        k.addDiode("D1", out, 0, d);
        k.addDiode("D2", 0, out, d);
        const auto tr = solveTransient(k, 5e-3, 5e-6, {}, 1 << 20);
        checkTrue("clipper transient completes", tr.ok, tr.error);
        double peak = 0.0;
        for (const auto& s : tr.voltages) peak = std::max(peak, s[(size_t)out]);
        checkTrue("clipper clips between 0.6 and 0.9 V", peak > 0.6 && peak < 0.9, "(" + std::to_string(peak) + ")");
    }

    // 21. Pole-zero cancellation: an RC low-pass driven by V1 and an unrelated RC elsewhere.
    //     H = V(out)/V1 has the one pole -1000 rad/s; the other RC's mode cancels.
    {
        Circuit c;
        auto in = c.addNode(), out = c.addNode(), other = c.addNode(), node = c.addNode();
        const auto v1 = c.addVoltageSource("V1", in, 0, dc(0.0));
        c.addResistor("R", in, out, 1000.0);
        c.addCapacitor("C", out, 0, 1e-6);
        c.addVoltageSource("V2", other, 0, dc(1.0));
        c.addResistor("R2", other, node, 2000.0);
        c.addCapacitor("C2", node, 0, 1e-6);
        const auto pz = solvePoleZero(c, out, 0, v1);
        checkTrue("PZ with an unrelated mode: 1 pole, 0 zeros", pz.ok && pz.poles.size() == 1 && pz.zeros.empty(),
                  "(" + std::to_string(pz.poles.size()) + " poles, " + std::to_string(pz.zeros.size()) + " zeros, " + std::to_string(pz.cancelled) + " cancelled)");
        check("PZ cancelled pairs", (double)pz.cancelled, 1.0, 0.0);
        if (!pz.poles.empty()) check("PZ remaining pole (rad/s)", pz.poles[0].real(), -1000.0, 1e-3);
    }

    // 22. Op amp macro-model (gain stage -> dominant pole -> rail-limited output), A0 = 200k,
    //     GBW = 1 MHz, +/-15 V, as an inverting x-1 amplifier (10k/10k) at 1 kHz, 1 V peak in:
    //     A(j 1 kHz) = A0/(1 + j f/fp) ~ -j1000 (fp = 5 Hz), noise gain 2:
    //     |Vout| = 1/|1 + 2/(-j1000)| = 0.999998 V peak, undistorted.
    {
        Circuit c;
        auto in = c.addNode(), inv = c.addNode(), out = c.addNode(), vp = c.addNode(), vn = c.addNode();
        auto stage = c.addNode(), pole = c.addNode();
        c.addVoltageSource("V1", in, 0, sine(1.0, 1000.0));
        c.addVoltageSource("VP", vp, 0, dc(15.0));
        c.addVoltageSource("VN", 0, vn, dc(15.0));
        c.addResistor("RI", in, inv, 10e3);
        c.addResistor("RF", inv, out, 10e3);
        OpAmpModel g; g.gain = 2e5; g.limited = false;
        c.addOpAmp("U1", 0, inv, stage, vp, vn, g);
        c.addResistor("U1.rp", stage, pole, 1e3);
        c.addCapacitor("U1.cp", pole, 0, 1.0 / (2.0 * 3.14159265358979 * (1e6 / 2e5) * 1e3));
        OpAmpModel o; o.gain = 1.0;
        c.addOpAmp("U1.out", pole, 0, out, vp, vn, o);
        const auto tr = solveTransient(c, 6e-3, 2e-6, {}, 1 << 20);
        checkTrue("op amp macro transient completes", tr.ok, tr.error);
        std::vector<double> vo;
        for (const auto& s : tr.voltages) vo.push_back(s[(size_t)out]);
        const auto fr = fourier(tr.time, vo, 1000.0, 5, 1);
        check("op amp macro |Vout| at 1 kHz (V)", fr.ok ? fr.magnitude[0] : 0.0, 0.999998, 1e-4);
        checkTrue("op amp macro THD < 0.05 %", fr.ok && fr.thdPercent < 0.05, "(" + std::to_string(fr.thdPercent) + ")");
    }

    // 23. Initial capacitor voltage: 1 uF charged to 5 V discharges through 1 k.
    {
        Circuit c;
        auto out = c.addNode();
        c.addResistor("R", out, 0, 1000.0);
        const auto cap = c.addCapacitor("C", out, 0, 1e-6);
        c.elements()[(size_t)cap].hasInitialCondition = true;
        c.elements()[(size_t)cap].initialCondition = 5.0;
        const auto tr = solveTransient(c, 5e-3, 10e-6, {}, 1 << 20);
        checkTrue("RC initial-voltage transient solves", tr.ok, tr.error);
        check("RC starts at initial voltage (V)", tr.voltages.empty() ? 0.0 : tr.voltages.front()[(size_t)out], 5.0, 1e-9);
        check("RC decays after one tau (V)", tr.voltages.size() > 100 ? tr.voltages[100][(size_t)out] : 0.0, 5.0 * std::exp(-1.0), 0.03);
    }

    // 24. Initial inductor current: 10 mH with 100 mA through 100 ohms decays.
    {
        Circuit c;
        auto out = c.addNode();
        const auto ind = c.addInductor("L", out, 0, 10e-3);
        c.addResistor("R", out, 0, 100.0);
        c.elements()[(size_t)ind].hasInitialCondition = true;
        c.elements()[(size_t)ind].initialCondition = 0.1;
        const auto tr = solveTransient(c, 1e-3, 1e-6, {}, 1 << 20);
        checkTrue("RL initial-current transient solves", tr.ok, tr.error);
        check("RL starts at initial current (A)", tr.sourceCurrents.empty() ? 0.0 : tr.sourceCurrents.front()[(size_t)ind], 0.1, 1e-9);
        checkTrue("RL current decays", tr.sourceCurrents.size() > 100 && std::abs(tr.sourceCurrents[100][(size_t)ind]) < 0.1,
                  tr.sourceCurrents.size() > 100 ? "(" + std::to_string(tr.sourceCurrents[100][(size_t)ind]) + ")" : "");
    }

    std::printf("\n-- circuit hierarchy tree --\n");
    {
        using circuit_hierarchy::flatten;
        auto describe = [](const std::vector<circuit_hierarchy::Row>& rows) {
            std::string s;
            for (const auto& r : rows)
                s += r.name + ":" + std::to_string(r.depth) + (r.missingParent ? "!" : "") + " ";
            return s;
        };
        const auto empty = flatten({}, "Main");
        checkTrue("hierarchy empty diagram is just the top level", describe(empty) == "Main:0 ", describe(empty));
        // A and C on the top level, B inside A, D's parent is gone, X/Y form a cycle,
        // and a sub_block with no sheet id is not a navigable sheet.
        const auto rows = flatten({ { "s1", "A", "" }, { "s2", "B", "s1" }, { "s3", "C", "" }, { "s4", "D", "ghost" },
                                    { "s5", "X", "s6" }, { "s6", "Y", "s5" }, { "", "NoSheet", "" } }, "Main");
        checkTrue("hierarchy full tree, nested and expanded in order",
                  describe(rows) == "Main:0 A:1 B:2 C:1 D:1! X:1! Y:2 ", describe(rows));
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

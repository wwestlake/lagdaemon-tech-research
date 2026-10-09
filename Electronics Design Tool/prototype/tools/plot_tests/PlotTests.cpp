// 2D/3D plotting instrument checks: probe resolution, synchronised
// acquisition from a real transient run, differential and current signals,
// missing probes, bad datasets, camera persistence and display thinning.
// Expected values are worked out from the circuit before running:
//   V1: 1 V 1 kHz sine on net a; V2: 1 V 1 kHz sine, 90 deg, on net b;
//   R2/R3 1k/1k divide b to net c, so V(c) = V(b)/2 and I(R3) = V(c)/1k.

#include <JuceHeader.h>
#include "../../Source/PlotInstrument.h"

#include <cmath>
#include <cstdio>

namespace pi = plot_instrument;

namespace
{
int failures = 0;

void checkTrue(const char* name, bool condition, const juce::String& detail = {})
{
    std::printf("%s  %s %s\n", condition ? "PASS" : "FAIL", name, detail.toRawUTF8());
    if (!condition) ++failures;
}

struct Bench
{
    analytics::Netlist n;
    circuit_sim::Waveform w1, w2;
    int a = 0, b = 0, c = 0;
};

Bench makeBench()
{
    Bench x;
    auto& circuit = x.n.circuit;
    x.a = circuit.addNode();
    x.b = circuit.addNode();
    x.c = circuit.addNode();
    x.w1.kind = circuit_sim::Waveform::Kind::Sine;
    x.w1.amplitude = 1.0;
    x.w1.frequency = 1000.0;
    x.w2 = x.w1;
    x.w2.phaseDegrees = 90.0;
    const auto v1 = circuit.addVoltageSource("V1", x.a, 0, x.w1);
    const auto v2 = circuit.addVoltageSource("V2", x.b, 0, x.w2);
    const auto r1 = circuit.addResistor("R1", x.a, 0, 1000.0);
    const auto r2 = circuit.addResistor("R2", x.b, x.c, 1000.0);
    const auto r3 = circuit.addResistor("R3", x.c, 0, 1000.0);
    x.n.nets = { { "a", x.a, "V1.1 R1.1" }, { "b", x.b, "V2.1 R2.1" }, { "c", x.c, "R2.2 R3.1" } };
    x.n.parts = { { "V1", "voltage_source", v1 }, { "V2", "voltage_source", v2 }, { "R1", "resistor", r1 },
                  { "R2", "resistor", r2 }, { "R3", "resistor", r3 } };
    return x;
}

analytics::Result runPlan(const Bench& x, const pi::Plan& plan)
{
    return analytics::run(analytics::Analysis::Transient,
                          { { "outputs", plan.outputs }, { "stop", "2m" }, { "step", "5u" }, { "max_points", "200000" } }, x.n);
}

double maxError(const pi::Acquisition& acq, int signal, const std::function<double(double)>& expected)
{
    double worst = 0.0;
    for (size_t i = 0; i < acq.size(); ++i)
        worst = std::max(worst, std::abs(acq.values[(size_t)signal][i] - expected(acq.time[i])));
    return worst;
}
}

int main()
{
    const auto bench = makeBench();
    const std::vector<pi::Channel> channels { { "A", bench.a, -1 }, { "B", bench.b, -1 }, { "C", bench.c, -1 } };

    std::printf("-- 1. two-probe XY --\n");
    {
        const auto plan = pi::plan(bench.n, channels, pi::Mode::XY, { "A", "B" });
        checkTrue("XY plan runs", plan.canRun(), plan.problems.joinIntoString("; "));
        checkTrue("XY outputs are the two probed nets", plan.outputs == "V(a), V(b)", plan.outputs);
        const auto acq = pi::acquire(runPlan(bench, plan), plan);
        checkTrue("XY acquisition ok", acq.ok, acq.error);
        checkTrue("XY has many samples", acq.size() > 300, juce::String((int)acq.size()));
        checkTrue("X is V(a) at each instant", maxError(acq, 0, [&](double t) { return bench.w1.valueAt(t); }) < 1e-6);
        checkTrue("Y is V(b) at the same instant", maxError(acq, 1, [&](double t) { return bench.w2.valueAt(t); }) < 1e-6);
        double radius = 0.0;
        for (size_t i = 0; i < acq.size(); ++i)
            radius = std::max(radius, std::abs(std::hypot(acq.values[0][i], acq.values[1][i]) - 1.0));
        checkTrue("quadrature sines trace the unit circle", radius < 1e-6, juce::String(radius, 9));
        checkTrue("labels name the channel and net", acq.labels[0] == "A: V(a)" && acq.units[0] == "V", acq.labels[0]);
        checkTrue("one run, no resampling", !acq.resampled);
    }

    std::printf("-- 2. three-probe XYZ --\n");
    {
        const auto plan = pi::plan(bench.n, channels, pi::Mode::XYZ, { "A", "B", "C" });
        const auto acq = pi::acquire(runPlan(bench, plan), plan);
        checkTrue("XYZ acquisition ok", acq.ok && acq.values.size() == 3, acq.error);
        checkTrue("Z is V(c) = V(b)/2", maxError(acq, 2, [&](double t) { return bench.w2.valueAt(t) / 2.0; }) < 1e-6);
        const auto xy = pi::plan(bench.n, channels, pi::Mode::XYZ, { "A", "B" });
        checkTrue("XYZ with two signals cannot run", !xy.canRun() && xy.problems.size() == 1, xy.problems.joinIntoString("; "));
    }

    std::printf("-- 3. nonuniform time synchronisation --\n");
    {
        const std::vector<double> t1 { 0.0, 1.0, 3.0, 4.0 }, v1 { 0.0, 1.0, 3.0, 4.0 };   // v = t
        const std::vector<double> t2 { 0.0, 2.0, 4.0, 5.0 }, v2 { 0.0, 4.0, 8.0, 10.0 };  // v = 2t
        const auto acq = pi::synchronise({ { "s1", "V", { &t1, &v1 }, {} }, { "s2", "V", { &t2, &v2 }, {} } });
        checkTrue("different time bases synchronise", acq.ok && acq.resampled, acq.error);
        checkTrue("common base is the union inside the overlap",
                  acq.time == std::vector<double>({ 0.0, 1.0, 2.0, 3.0, 4.0 }), juce::String((int)acq.size()));
        bool exact = acq.ok;
        for (size_t i = 0; exact && i < acq.size(); ++i)
            exact = std::abs(acq.values[0][i] - acq.time[i]) < 1e-12 && std::abs(acq.values[1][i] - 2.0 * acq.time[i]) < 1e-12;
        checkTrue("each point pairs values of the same instant", exact);

        const std::vector<double> t { 0.0, 1e-9, 1e-6, 5e-6, 5e-6, 2e-3 }, x { 1, 2, 3, 4, 5, 6 }, y { 6, 5, 4, 3, 2, 1 };
        const auto same = pi::synchronise({ { "x", "V", { &t, &x }, {} }, { "y", "V", { &t, &y }, {} } });
        bool untouched = same.ok && !same.resampled && same.time == t;
        for (size_t i = 0; untouched && i < t.size(); ++i)
            untouched = same.values[0][i] == x[i] && same.values[1][i] == y[i];
        checkTrue("a shared nonuniform base keeps every solver step unchanged", untouched);

        const std::vector<double> late { 10.0, 11.0 }, lv { 1.0, 1.0 };
        const auto apart = pi::synchronise({ { "s1", "V", { &t1, &v1 }, {} }, { "late", "V", { &late, &lv }, {} } });
        checkTrue("disjoint time ranges are refused", !apart.ok && apart.error.contains("overlap"), apart.error);
    }

    std::printf("-- 4. differential voltage --\n");
    {
        std::vector<pi::Channel> diff { { "A", bench.b, bench.c } };
        const auto plan = pi::plan(bench.n, diff, pi::Mode::Time, { "A", "V(a,c)" });
        checkTrue("differential plan", plan.canRun() && plan.signals[0].minusTrace == "V(c)", plan.problems.joinIntoString("; "));
        const auto acq = pi::acquire(runPlan(bench, plan), plan);
        checkTrue("A = V(b) - V(c) = V(b)/2", acq.ok && maxError(acq, 0, [&](double t) { return bench.w2.valueAt(t) / 2.0; }) < 1e-6, acq.error);
        checkTrue("V(a,c) = V(a) - V(b)/2", acq.ok && maxError(acq, 1, [&](double t) { return bench.w1.valueAt(t) - bench.w2.valueAt(t) / 2.0; }) < 1e-6);
        checkTrue("differential label", acq.ok && acq.labels[0] == "A: V(b)-V(c)", acq.labels[0]);
        std::vector<pi::Channel> shorted { { "A", bench.b, bench.b } };
        const auto bad = pi::plan(bench.n, shorted, pi::Mode::Time, { "A" });
        checkTrue("both pins on one net is reported", !bad.canRun() && bad.problems[0].contains("same net"), bad.problems.joinIntoString("; "));
    }

    std::printf("-- 5. branch current --\n");
    {
        const auto plan = pi::plan(bench.n, channels, pi::Mode::XY, { "C", "I(R3)" });
        const auto acq = pi::acquire(runPlan(bench, plan), plan);
        checkTrue("current acquisition ok", acq.ok && acq.units[1] == "A", acq.error);
        checkTrue("|I(R3)| = |V(c)|/1k at each instant",
                  acq.ok && maxError(acq, 1, [&](double t) { return 0.0; }) > 1e-4 // nonzero
                      && [&] { double w = 0; for (size_t i = 0; i < acq.size(); ++i) w = std::max(w, std::abs(std::abs(acq.values[1][i]) - std::abs(acq.values[0][i]) / 1000.0)); return w < 1e-9; }());
        const auto missing = pi::plan(bench.n, channels, pi::Mode::XY, { "A", "I(R99)" });
        checkTrue("unknown part current is reported", !missing.canRun() && missing.problems[0].contains("R99"), missing.problems.joinIntoString("; "));
    }

    std::printf("-- 6. configuration persistence (camera) --\n");
    {
        pi::Camera cam;
        cam.yaw = 47.5; cam.pitch = -12.0; cam.zoom = 2.25; cam.panX = 0.125; cam.panY = -0.5;
        const auto back = pi::parseCamera(pi::toString(cam));
        checkTrue("camera survives text round trip",
                  back.yaw == 47.5 && back.pitch == -12.0 && back.zoom == 2.25 && back.panX == 0.125 && back.panY == -0.5, pi::toString(back));
        const auto junk = pi::parseCamera("yaw=abc pitch=200 zoom=");
        checkTrue("bad camera text keeps safe values", junk.yaw == pi::Camera().yaw && junk.pitch == 89.0 && junk.zoom == 1.0, pi::toString(junk));
        checkTrue("modes parse back", pi::parseMode(pi::modeName(pi::Mode::XYZ)) == pi::Mode::XYZ
                                       && pi::parseMode("Time") == pi::Mode::Time && pi::parseMode("garbage") == pi::Mode::XY);
    }

    std::printf("-- 7. missing / disconnected probes --\n");
    {
        std::vector<pi::Channel> open { { "A", bench.a, -1 }, { "B", -1, -1 }, { "C", 999, -1 } };
        const auto xy = pi::plan(bench.n, open, pi::Mode::XY, { "A", "B" });
        checkTrue("XY with an unwired channel cannot run", !xy.canRun());
        checkTrue("unwired channel says what to wire", xy.problems.size() == 1 && xy.problems[0].contains("wire B+"), xy.problems.joinIntoString("; "));
        const auto time = pi::plan(bench.n, open, pi::Mode::Time, { "A", "B", "C" });
        checkTrue("time mode runs the connected traces", time.canRun() && time.problems.size() == 2, time.problems.joinIntoString("; "));
        checkTrue("net outside the circuit is reported", time.problems[1].contains("not part of the simulated circuit"), time.problems[1]);
        const auto acq = pi::acquire(runPlan(bench, time), time);
        checkTrue("time acquisition keeps the good trace and warns", acq.ok && acq.values.size() == 1 && acq.warnings.size() >= 2, acq.error);
        const auto unknown = pi::plan(bench.n, open, pi::Mode::XY, { "A", "Q" });
        checkTrue("unknown spec is explained", unknown.problems[0].contains("unknown signal"), unknown.problems.joinIntoString("; "));
    }

    std::printf("-- 8. empty and invalid datasets --\n");
    {
        const std::vector<double> empty;
        const std::vector<double> t { 0.0, 1.0, 0.5 }, v { 1.0, 2.0, 3.0 }, shortV { 1.0 };
        checkTrue("empty series refused", !pi::synchronise({ { "e", "V", { &empty, &empty }, {} } }).ok);
        checkTrue("backwards time refused", pi::synchronise({ { "b", "V", { &t, &v }, {} } }).error.contains("backwards"));
        checkTrue("size mismatch refused", pi::synchronise({ { "m", "V", { &t, &shortV }, {} } }).error.contains("values"));
        checkTrue("no signals refused", !pi::synchronise({}).ok);
        const std::vector<double> tg { 0.0, 1.0, 2.0 }, vg { 1.0, std::nan(""), 3.0 };
        const auto gap = pi::synchronise({ { "g", "V", { &tg, &vg }, {} } });
        checkTrue("NaN sample becomes a counted gap", gap.ok && gap.gaps == 1 && std::isnan(gap.values[0][1]), juce::String(gap.gaps));
        analytics::Result failed;
        failed.error = "Transient failed: singular matrix";
        const auto plan = pi::plan(bench.n, channels, pi::Mode::XY, { "A", "B" });
        const auto acq = pi::acquire(failed, plan);
        checkTrue("failed run reports its error", !acq.ok && acq.error.contains("singular"), acq.error);
        checkTrue("range of no finite values is invalid", !pi::autoRange({ std::nan("") }).valid);
        const auto flat = pi::autoRange({ 2.0, 2.0 });
        checkTrue("flat signal gets a usable range", flat.valid && flat.min < 2.0 && flat.max > 2.0);
        const auto manual = pi::applyManual(pi::autoRange({ 0.0, 1.0 }), "-2", "auto");
        checkTrue("manual min with auto max", manual.min == -2.0 && std::abs(manual.max - 1.05) < 1e-12);
        const auto inverted = pi::applyManual(pi::autoRange({ 0.0, 1.0 }), "5", "1");
        checkTrue("inverted manual range falls back to auto", inverted.min < 0.0);
    }

    std::printf("-- 9. switching modes leaves the circuit alone --\n");
    {
        auto x = makeBench();
        const auto elements = x.n.circuit.elements().size();
        const auto nodes = x.n.circuit.nodeCount();
        for (auto mode : { pi::Mode::Time, pi::Mode::XY, pi::Mode::XYZ, pi::Mode::XY, pi::Mode::Time })
        {
            const auto plan = pi::plan(x.n, channels, mode, { "A", "B", "C" });
            const auto acq = pi::acquire(runPlan(x, plan), plan);
            if (!acq.ok) checkTrue("mode run", false, pi::modeName(mode) + ": " + acq.error);
        }
        checkTrue("no element or node added by any mode", x.n.circuit.elements().size() == elements && x.n.circuit.nodeCount() == nodes);
    }

    std::printf("-- 10. display data for a large dataset --\n");
    {
        const int n = 1000000;
        std::vector<double> t((size_t)n), x((size_t)n), y((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            t[(size_t)i] = i * 1e-7 + (i % 3) * 1e-9; // nonuniform steps
            x[(size_t)i] = std::sin(i * 0.0007) + 0.3 * std::sin(i * 0.031);
            y[(size_t)i] = std::cos(i * 0.0011);
        }
        x[777777] = 9.0;   // a single-sample spike must survive thinning
        y[123457] = -7.0;
        y[500000] = std::nan("");
        const auto acq = pi::synchronise({ { "x", "V", { &t, &x }, {} }, { "y", "V", { &t, &y }, {} } });
        const auto copyX = acq.values[0];
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto a = pi::displayIndices(acq, { 0, 1 }, 6000);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        const auto b = pi::displayIndices(acq, { 0, 1 }, 6000);
        bool ordered = !a.empty();
        for (size_t i = 1; ordered && i < a.size(); ++i) ordered = a[i] > a[i - 1];
        auto has = [&](int index) { return std::binary_search(a.begin(), a.end(), index); };
        checkTrue("thinned to the budget", a.size() <= 6001 && a.size() > 3000, juce::String((int)a.size()));
        checkTrue("chronological, no repeats", ordered);
        checkTrue("deterministic", a == b);
        checkTrue("first and last samples kept", a.front() == 0 && a.back() == n - 1);
        checkTrue("spike extrema kept", has(777777) && has(123457));
        checkTrue("gap kept so the line breaks", has(500000));
        checkTrue("acquisition data untouched", acq.values[0] == copyX && acq.size() == (size_t)n);
        checkTrue("thinning a million samples is fast", ms < 500.0, juce::String(ms, 1) + " ms");
        const auto small = pi::displayIndices(acq, { 0 }, 0);
        checkTrue("no budget keeps every sample", small.size() == (size_t)n);
    }

    std::printf("-- projection and ticks --\n");
    {
        pi::Camera front;
        front.yaw = 0.0; front.pitch = 0.0; front.perspective = false;
        const juce::Rectangle<float> area { 0.0f, 0.0f, 200.0f, 100.0f };
        const auto right = pi::project(front, 1.0, 0.0, 0.0, area), up = pi::project(front, 0.0, 0.0, 1.0, area);
        checkTrue("front view: +X goes right", right.x > 100.0f && std::abs(right.y - 50.0f) < 1e-4f);
        checkTrue("front view: +Z goes up", up.y < 50.0f && std::abs(up.x - 100.0f) < 1e-4f);
        pi::Camera top = front;
        top.pitch = 89.0;
        checkTrue("top view: +Y goes up the screen", pi::project(top, 0.0, 1.0, 0.0, area).y < 50.0f);
        const auto ticks = pi::niceTicks(-0.93, 2.1, 6);
        checkTrue("nice ticks", ticks.size() >= 4 && ticks.front() == -0.5 && std::abs(ticks[1]) < 1e-12, juce::String((int)ticks.size()));
        bool ok = false;
        checkTrue("engineering numbers", std::abs(pi::parseNumber("4.7k", 0, &ok) - 4700.0) < 1e-9 && ok
                                          && std::abs(pi::parseNumber("10u", 0) - 1e-5) < 1e-18
                                          && std::abs(pi::parseNumber("2meg", 0) - 2e6) < 1e-6
                                          && std::abs(pi::parseNumber("1e-3s", 0) - 1e-3) < 1e-15);
        pi::parseNumber("auto", 0, &ok);
        checkTrue("non-number refused", !ok);
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

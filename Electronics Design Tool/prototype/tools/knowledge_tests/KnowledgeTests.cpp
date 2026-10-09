// Knowledge and capability checks: LiteSemRAG retrieval against the real card
// corpus, the application capability catalog, and the Chua reference values
// the cards document (simulated here with both engines).

#include <JuceHeader.h>
#include "../../Source/XyceBackend.h"
#include "ChuaReference.h"
#include "../../Source/SpiceLibrary.h"

#include <cstdio>
#include <tuple>

namespace
{
int failures = 0;

void checkTrue(const char* name, bool condition, const juce::String& detail = {})
{
    std::printf("%s  %s %s\n", condition ? "PASS" : "FAIL", name, detail.toRawUTF8());
    if (!condition) ++failures;
}

chua_reference::Verdict internalRun(const chua_reference::Values& v, double stop = 0.12)
{
    // The internal solver cannot start this circuit from a capacitor initial
    // voltage (the first step does not converge); a small inductor current works.
    auto b = chua_reference::opAmpRealization(v);
    b.netlist.circuit.elements()[0].hasInitialCondition = false;
    auto& l = b.netlist.circuit.elements()[(size_t)b.netlist.circuit.find("L1")];
    l.hasInitialCondition = true;
    l.initialCondition = 1e-4;
    circuit_sim::TransientSettings ts;
    ts.stop = stop;
    ts.step = 1e-6;
    ts.maxSamples = 200000;
    const auto tr = circuit_sim::solveTransient(b.netlist.circuit, ts);
    if (!tr.ok)
    {
        std::printf("  internal solver failed: %s\n", tr.error.c_str());
        return {};
    }
    std::vector<double> v1(tr.time.size());
    for (size_t i = 0; i < v1.size(); ++i)
        v1[i] = tr.voltages[i][(size_t)b.v1];
    return chua_reference::classify(tr.time, v1, 0.02);
}

chua_reference::Verdict xyceRun(const chua_reference::Values& v, double stop = 0.12)
{
    const auto b = chua_reference::behavioralRealization(v);
    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_knowledge_tests");
    out.createDirectory();
    const auto r = xyce_backend::run(analytics::Analysis::Transient,
                                     { { "outputs", "V(V1)" }, { "stop", juce::String(stop) }, { "step", "1u" } }, b.netlist, out);
    if (!r.ok)
    {
        std::printf("  xyce failed: %s\n", r.error.toRawUTF8());
        return {};
    }
    const auto* t = analytics::findTrace(r, "V(V1)");
    if (t == nullptr)
        return {};
    return chua_reference::classify(t->x, t->y, 0.02);
}

void describe(const char* engine, double r, const chua_reference::Verdict& d)
{
    std::printf("  %-8s R=%6.0f  %-13s switches=%4d spread=%.2f  V1 %.2f..%.2f\n", engine, r, chua_reference::name(d.behaviour),
                d.switches, d.spread, d.minV, d.maxV);
}
}

int main(int argc, char** argv)
{
    const bool sweep = argc > 1 && juce::String(argv[1]) == "--sweep";
    juce::String detail;
    const bool xyce = xyce_backend::isConfigured(detail);

    if (argc > 1 && juce::String(argv[1]) == "--opamp")
    {
        chua_reference::Values v;
        for (auto [label, ic, step] : { std::tuple<const char*, double, double> { "ic 0.1, 1us", 0.1, 1e-6 },
                                        { "no ic, 1us", 0.0, 1e-6 }, { "ic 0.1, 0.1us", 0.1, 1e-7 } })
        {
            v.initialC1 = ic;
            auto b = chua_reference::opAmpRealization(v);
            if (ic == 0.0)
                b.netlist.circuit.elements()[0].hasInitialCondition = false;
            const auto op = circuit_sim::solveOperatingPoint(b.netlist.circuit);
            std::printf("  %s: op %s %s v1=%g\n", label, op.ok ? "ok" : "FAILED", op.error.c_str(), op.ok ? op.voltages[(size_t)b.v1] : 0.0);
            circuit_sim::TransientSettings ts;
            ts.stop = 0.005;
            ts.step = step;
            const auto tr = circuit_sim::solveTransient(b.netlist.circuit, ts);
            std::printf("    transient %s %s samples=%d\n", tr.ok ? "ok" : "FAILED", tr.error.c_str(), (int)tr.time.size());
        }
        {
            // Kick through the inductor current instead of a capacitor voltage.
            v.initialC1 = 0.0;
            auto b = chua_reference::opAmpRealization(v);
            b.netlist.circuit.elements()[0].hasInitialCondition = false;
            auto& l = b.netlist.circuit.elements()[(size_t)b.netlist.circuit.find("L1")];
            l.hasInitialCondition = true;
            l.initialCondition = 1e-4;
            circuit_sim::TransientSettings ts;
            ts.stop = 0.12;
            ts.step = 1e-6;
            ts.maxSamples = 200000;
            const auto tr = circuit_sim::solveTransient(b.netlist.circuit, ts);
            std::printf("  internal, L1 initial current 0.1 mA: %s %s\n", tr.ok ? "ok" : "FAILED", tr.error.c_str());
            if (tr.ok)
            {
                std::vector<double> v1(tr.time.size());
                for (size_t i = 0; i < v1.size(); ++i) v1[i] = tr.voltages[i][(size_t)b.v1];
                describe("int-L-ic", v.r, chua_reference::classify(tr.time, v1, 0.02));
            }
            v.initialC1 = 0.1;
        }
        if (xyce)
        {
            auto b = chua_reference::opAmpRealization(v);
            spice_library::initialize();
            for (auto& e : b.netlist.circuit.elements())
                if (e.name == "A1" || e.name == "A2")
                    e.modelName = "uA741";
            const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_knowledge_tests");
            const auto r = xyce_backend::run(analytics::Analysis::Transient, { { "outputs", "V(V1)" }, { "stop", "0.12" }, { "step", "1u" } }, b.netlist, out);
            if (!r.ok) std::printf("  xyce op-amp: FAILED %s\n", r.error.toRawUTF8());
            else if (const auto* t = analytics::findTrace(r, "V(V1)")) describe("xyce-op", v.r, chua_reference::classify(t->x, t->y, 0.02));
        }
        return 0;
    }
    if (sweep)
    {
        for (double r = 1500.0; r <= 2100.0; r += 50.0)
        {
            chua_reference::Values v;
            v.r = r;
            describe("internal", r, internalRun(v));
            if (xyce)
                describe("xyce", r, xyceRun(v));
        }
        return 0;
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}



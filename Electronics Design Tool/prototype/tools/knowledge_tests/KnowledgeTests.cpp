// Knowledge and capability checks: LiteSemRAG retrieval against the real card
// corpus, the application capability catalog, and the Chua reference values
// the cards document (simulated here with both engines).

#include <JuceHeader.h>
#include "../../Source/XyceBackend.h"
#include "ChuaReference.h"
#include "../../Source/SpiceLibrary.h"
#include "../../Source/ElectronicsKnowledge.h"
#include "../../Source/CapabilityCatalog.h"

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

    namespace ek = electronics_knowledge;
    auto ids = [](const ek::RetrievalResult& r, int n) {
        juce::StringArray out;
        for (int i = 0; i < n && i < (int)r.cards.size(); ++i)
            out.add(r.cards[(size_t)i].id);
        return out;
    };
    auto rankOf = [](const ek::RetrievalResult& r, const juce::String& idStart) {
        for (int i = 0; i < (int)r.cards.size(); ++i)
            if (r.cards[(size_t)i].id.startsWith(idStart))
                return i;
        return 1000;
    };
    auto firstMatching = [](const ek::RetrievalResult& r, const juce::String& idPart) {
        for (int i = 0; i < (int)r.cards.size(); ++i)
            if (r.cards[(size_t)i].id.containsIgnoreCase(idPart))
                return i;
        return 1000;
    };

    std::printf("-- 1. the original Chua prompt ranks the Chua cards first --\n");
    {
        const juce::String prompt =
            "Design, construct, simulate, and visualize a working **Chua chaotic oscillator** in the current scratch project. "
            "You are the electrical engineer responsible for the complete task. Use your available Workbench tools autonomously. "
            "Circuit requirements: Two capacitors (C1, C2). One inductor (L). A coupling resistor (R). A nonlinear negative-resistance "
            "element (Chua diode), using an appropriate available implementation. Suitable component values, models, grounding, and "
            "initial conditions. Research or derive reasonable circuit parameters. Construct the complete schematic, establish correct "
            "electrical connections, and run ERC. Simulation: Configure and run transient analysis. Measure the voltages across both "
            "capacitors and the current through the inductor. Visualization: produce an XY phase-space plot with V(C1) on X and V(C2) on Y. "
            "If a 3D plotting instrument is available, produce an XYZ plot. Look for the characteristic double-scroll chaotic attractor.";
        const auto r = ek::retrieve(prompt, 8);
        std::printf("   top: %s\n", ids(r, 8).joinIntoString(" | ").toRawUTF8());
        checkTrue("Chua topology card is first", rankOf(r, "electronics.reference.chua_circuit") == 0, ids(r, 3).joinIntoString(", "));
        int chua = 0;
        for (const auto& c : r.cards) chua += c.id.startsWith("electronics.reference.chua_circuit") ? 1 : 0;
        checkTrue("all three Chua cards retrieved", chua == 3, juce::String(chua));
        const auto sensing = firstMatching(r, "current_sensing");
        checkTrue("every Chua card ranks above any current-sensing card", sensing > 2 || sensing == 1000, juce::String(sensing));
        checkTrue("plotter card retrieved for the plotting request", rankOf(r, "electronics.reference.plotter_instrument") < 8);
    }

    std::printf("-- 2. 'current scratch project' does not drive retrieval --\n");
    {
        const auto r = ek::retrieve("Build an RC low-pass filter in the current scratch project", 8);
        std::printf("   top: %s\n", ids(r, 5).joinIntoString(" | ").toRawUTF8());
        checkTrue("no current-sensing card in the top 3", firstMatching(r, "current_sensing") >= 3, ids(r, 3).joinIntoString(", "));
        checkTrue("a low-pass card is first", r.cards.size() > 0 && (r.cards[0].id.containsIgnoreCase("low_pass") || r.cards[0].title.containsIgnoreCase("low-pass")
                                                                       || r.cards[0].title.containsIgnoreCase("low pass")), ids(r, 1).joinIntoString(""));
    }

    std::printf("-- 3. multiword engineering terms --\n");
    {
        const auto nic = ek::retrieve("negative impedance converter", 5);
        checkTrue("negative impedance converter -> NIC card first", rankOf(nic, "electronics.reference.negative_impedance_converter") == 0, ids(nic, 3).joinIntoString(", "));
        const auto bcs = ek::retrieve("how do I make a nonlinear resistor with a behavioral current source", 5);
        checkTrue("behavioral current source -> PWL card in top 2", rankOf(bcs, "electronics.reference.pwl_behavioral_source") <= 1, ids(bcs, 3).joinIntoString(", "));
        const auto ic = ek::retrieve("set an initial condition so the oscillator starts", 5);
        checkTrue("initial condition -> initial conditions card in top 2", rankOf(ic, "electronics.reference.initial_conditions") <= 1, ids(ic, 3).joinIntoString(", "));
    }

    std::printf("-- 4. component identifiers stay searchable --\n");
    {
        const auto op = ek::retrieve("opamp_generic", 5);
        checkTrue("opamp_generic finds a card that uses it", op.cards.size() > 0 && op.cards[0].text.contains("opamp_generic"), ids(op, 3).joinIntoString(", "));
        const auto wc = ek::retrieve("workbench_capabilities", 3);
        checkTrue("workbench_capabilities tool card first", rankOf(wc, "electronics.tool.workbench_capabilities") == 0, ids(wc, 3).joinIntoString(", "));
        const auto plot = ek::retrieve("xyz_plotter", 3);
        checkTrue("xyz_plotter finds the plotter card", rankOf(plot, "electronics.reference.plotter_instrument") == 0, ids(plot, 3).joinIntoString(", "));
    }

    std::printf("-- 5. existing cards stay accessible --\n");
    {
        const auto all = ek::allCards();
        checkTrue("corpus loaded (old cards plus new)", all.size() >= 685, juce::String((int)all.size()));
        const auto ring = ek::retrieve("ring oscillator", 3);
        checkTrue("ring oscillator cookbook card first", rankOf(ring, "electronics.cookbook.item.oscillators.ring_oscillator") == 0, ids(ring, 3).joinIntoString(", "));
        const auto uart = ek::retrieve("UART electrical interface levels", 3);
        checkTrue("UART card first", firstMatching(uart, "uart") == 0, ids(uart, 3).joinIntoString(", "));
        const auto lookup = ek::retrieve("cookbook_lookup", 3);
        checkTrue("cookbook_lookup tool card first", rankOf(lookup, "electronics.tool.cookbook_lookup") == 0, ids(lookup, 3).joinIntoString(", "));
        checkTrue("empty query returns nothing", ek::retrieve("   ", 5).cards.empty());
    }

    auto catalog = [](juce::String section, juce::String query, juce::String symbolId) {
        return juce::JSON::parse(capability_catalog::toJson({ section, query, symbolId }));
    };
    auto hasId = [](const juce::var& list, const juce::String& id) {
        if (auto* a = list.getArray())
            for (const auto& e : *a)
                if (e.getProperty("id", {}).toString() == id)
                    return true;
        return false;
    };

    std::printf("-- 6. discovering the behavioral current source and the generic op amp --\n");
    {
        const auto b = catalog("components", "behavioral current", "");
        checkTrue("query finds behavioral_current_source", hasId(b.getProperty("components", {}), "behavioral_current_source"));
        const auto detail = catalog("", "", "behavioral_current_source");
        const auto comp = detail.getProperty("component", {});
        const auto pins = comp.getProperty("pins", {});
        checkTrue("its pins are + and -", pins.size() == 2 && pins[0].toString() == "+" && pins[1].toString() == "-");
        const auto params = comp.getProperty("parameters", {});
        checkTrue("its expression parameter is described", params.size() >= 1 && params[0].getProperty("help", {}).toString().contains("expression"),
                  juce::JSON::toString(params, true).substring(0, 160));
        const auto op = catalog("components", "opamp", "");
        checkTrue("query finds opamp_generic", hasId(op.getProperty("components", {}), "opamp_generic"));
        const auto opd = catalog("", "", "opamp_generic").getProperty("component", {});
        checkTrue("op amp pins include supplies", opd.getProperty("pins", {}).size() == 5);
        const auto summary = catalog("", "", "");
        checkTrue("summary lists analyses and engines", summary.getProperty("analyses", {}).size() >= 10 && summary.getProperty("engines", {}).isObject());
        checkTrue("place_symbol id list comes from the registry", capability_catalog::componentIdList().contains("behavioral_current_source"));
    }

    std::printf("-- 7. discovering the plotting instruments --\n");
    {
        const auto inst = catalog("instruments", "", "");
        const auto list = inst.getProperty("instruments", {});
        checkTrue("xyz_plotter is an instrument", hasId(list, "xyz_plotter"));
        bool modes = false;
        if (auto* a = list.getArray())
            for (const auto& e : *a)
                if (e.getProperty("id", {}).toString() == "xyz_plotter")
                    if (auto* ps = e.getProperty("parameters", {}).getArray())
                        for (const auto& p : *ps)
                            if (p.getProperty("key", {}).toString() == "mode")
                                modes = p.getProperty("options", {}).size() == 3;
        checkTrue("plotter modes Time / XY / XYZ are listed", modes);
        checkTrue("instrument tools are named", inst.getProperty("instrumentTools", {}).toString().contains("instrument_plot_data"));
        checkTrue("plot query finds the plotter", hasId(catalog("instruments", "plot", "").getProperty("instruments", {}), "xyz_plotter"));
    }

    std::printf("-- 8. unknown components are reported, not invented --\n");
    {
        const auto unknown = catalog("", "", "chua_diode");
        checkTrue("unknown id refused", !(bool)unknown.getProperty("ok", true) && unknown.getProperty("error", {}).toString().contains("No component type"));
        checkTrue("close matches offered", unknown.getProperty("closeMatches", {}).size() >= 1,
                  juce::JSON::toString(unknown.getProperty("closeMatches", {}), true));
        const auto none = catalog("components", "flux capacitor", "");
        checkTrue("no-match query says so", none.getProperty("components", {}).size() == 0 && none.getProperty("note", {}).toString().contains("no such component"));
    }

    std::printf("-- reference Chua circuits (values the cards document) --\n");
    {
        chua_reference::Values v;
        auto d = internalRun(v);
        describe("internal", v.r, d);
        checkTrue("op-amp realization, internal solver: double scroll at 1.8k", d.behaviour == chua_reference::Behaviour::DoubleScroll);
        v.r = 2050.0;
        d = internalRun(v);
        checkTrue("classifier: 2.05k settles (not chaos)", d.behaviour == chua_reference::Behaviour::Decayed, chua_reference::name(d.behaviour));
        v.r = 1650.0;
        d = internalRun(v);
        checkTrue("classifier: 1.65k periodic window (not chaos)", d.behaviour == chua_reference::Behaviour::Periodic, chua_reference::name(d.behaviour));
        if (xyce)
        {
            chua_reference::Values x;
            const auto dx = xyceRun(x);
            describe("xyce", x.r, dx);
            checkTrue("behavioral realization, Xyce: double scroll at 1.8k", dx.behaviour == chua_reference::Behaviour::DoubleScroll);
        }
        else
            std::printf("SKIP  Xyce not configured: %s\n", detail.toRawUTF8());

        const auto b = chua_reference::opAmpRealization(chua_reference::Values {});
        auto withKick = b.netlist;
        withKick.circuit.elements()[0].hasInitialCondition = false;
        auto& l = withKick.circuit.elements()[(size_t)withKick.circuit.find("L1")];
        l.hasInitialCondition = true;
        l.initialCondition = 1e-4;
        const auto r = analytics::run(analytics::Analysis::Transient, { { "outputs", "V(V1), I(L1)" }, { "stop", "30m" }, { "step", "1u" } }, withKick);
        const auto* il = analytics::findTrace(r, "I(L1)");
        double peak = 0.0;
        if (il != nullptr) for (auto y : il->y) peak = std::max(peak, std::abs(y));
        checkTrue("I(L1) is available as a plotted signal", r.ok && il != nullptr && peak > 1e-4, r.error + " peak=" + juce::String(peak));
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}



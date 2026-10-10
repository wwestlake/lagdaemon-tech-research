// Knowledge and capability checks: LiteSemRAG retrieval against the real card
// corpus, the application capability catalog, and the Chua reference values
// the cards document (simulated here with both engines).

#include <JuceHeader.h>
#include "../../Source/XyceBackend.h"
#include "ChuaReference.h"
#include "../../Source/SpiceLibrary.h"
#include "../../Source/ElectronicsKnowledge.h"
#include "../../Source/CapabilityCatalog.h"
#include "../../Source/ErcAdvice.h"
#include "../../Source/AgentProgress.h"
#include "../../Source/SchematicSymbols.h"

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

    std::printf("-- ERC advice: actionable findings --\n");
    {
        checkTrue("supply pins recognised by name", erc_advice::supplyPolarity("V+") == 1 && erc_advice::supplyPolarity("VEE") == -1
                                                   && erc_advice::supplyPolarity("vdd") == 1 && erc_advice::supplyPolarity("IN+") == 0 && erc_advice::supplyPolarity("OUT") == 0);
        erc_advice::Context ctx;
        ctx.supplyNets = { "+12V", "-12V" };
        ctx.groundNets = { "GND" };
        erc_advice::Finding f;
        f.severity = "ERROR";
        f.category = "unconnected_supply_pin";
        f.refdes = "U1";
        f.symbolId = "opamp_generic";
        f.pin = "V+";
        f.pinIndex = 3;
        f.connections = { "U1.IN+ on V1", "U1.OUT on n4" };
        erc_advice::suggest(f, ctx);
        const auto all = f.suggestions.joinIntoString(" ");
        checkTrue("supply fix names the pin and an existing positive rail", all.contains("U1.V+") && all.contains("+12V"), all.substring(0, 140));
        checkTrue("positive pin is not offered the negative rail", !f.suggestions[0].contains("-12V"), f.suggestions[0]);
        checkTrue("supply voltage is not hardcoded", !all.contains("9 V") && !all.contains("9V") && all.contains("rating"));
        // Every operation a suggestion names is a real tool, every part a real component type.
        static const juce::StringArray tools { "schematic_connect", "schematic_place_symbol", "schematic_set_parameters", "schematic_delete_components",
                                               "schematic_rename_component", "workbench_capabilities" };
        bool real = true;
        juce::String bad;
        for (const auto& category : { "unconnected_supply_pin", "unconnected_pin", "no_ground", "undriven_supply_net", "shorted_source", "shorted_part",
                                      "invalid_value", "missing_value", "duplicate_refdes", "unnamed_net_label" })
        {
            erc_advice::Finding g = f;
            g.category = category;
            g.suggestions.clear();
            g.net = "n4";
            erc_advice::suggest(g, ctx);
            if (g.suggestions.isEmpty()) { real = false; bad << category << " has no suggestion; "; }
            for (const auto& s : g.suggestions)
                for (const auto& word : juce::StringArray::fromTokens(s, " ,;:()", ""))
                {
                    if (word.startsWith("schematic_") || word.startsWith("workbench_"))
                        if (!tools.contains(word)) { real = false; bad << word << " "; }
                    for (const auto part : { "power_port", "voltage_source", "ground" })
                        if (word == part && !schematic::isSupportedSymbol(part)) { real = false; bad << part << " "; }
                }
        }
        checkTrue("suggestions use only real tools and components", real, bad);
        const auto v = erc_advice::toVar(f);
        checkTrue("finding carries part, pin label, index and connections",
                  v.getProperty("pinLabel", {}).toString() == "U1.V+" && (int)v.getProperty("pinIndex", -1) == 3
                      && v.getProperty("existingConnections", {}).size() == 2 && v.getProperty("suggestedActions", {}).size() >= 2);
        checkTrue("markdown line keeps the [SEVERITY] message format", erc_advice::markdownLine(f).startsWith("- [ERROR] "));
    }

    std::printf("-- no-progress guard --\n");
    {
        const juce::String erc = R"({"ok": true, "tool": "circuit_run_erc", "passed": false, "errors": 2, "findings": [{"pinLabel": "U1.V+"}], "reportPath": "C:/a/erc_report.md"})";
        const juce::String ercOtherPath = R"({"ok": true, "tool": "circuit_run_erc", "passed": false, "errors": 2, "findings": [{"pinLabel": "U1.V+"}], "reportPath": "C:/b/erc_report.md"})";
        {
            agent_progress::NoProgressGuard g;
            const bool s1 = g.record("circuit_run_erc", "{}", erc), s2 = g.record("circuit_run_erc", "{}", ercOtherPath), s3 = g.record("circuit_run_erc", "{}", erc);
            checkTrue("unchanged successful ERC stops on the third call", !s1 && !s2 && s3);
            checkTrue("reason names the tool and the lack of progress", g.reason().contains("circuit_run_erc") && g.reason().contains("lack of progress"));
        }
        {
            agent_progress::NoProgressGuard g;
            bool stopped = false;
            for (int i = 0; i < 12 && !stopped; ++i)
            {
                stopped = stopped || g.record("schematic_connect", R"({"a": "R1.2", "b": "C1.1"})", R"({"ok": true, "wires": )" + juce::String(i) + "}");
                stopped = stopped || g.record("circuit_run_erc", "{}", R"({"ok": true, "errors": )" + juce::String(12 - i) + "}");
            }
            checkTrue("repeated calls with changing results never stop", !stopped);
        }
        {
            agent_progress::NoProgressGuard g;
            bool stopped = false;
            int calls = 0;
            for (int i = 0; i < 6 && !stopped; ++i)
            {
                ++calls;
                stopped = g.record(i % 2 == 0 ? "circuit_run_erc" : "circuit_inspect", "{}", i % 2 == 0 ? erc : R"({"ok": true, "parts": 8})");
            }
            checkTrue("alternating unchanged checks also stop", stopped && calls == 6, juce::String(calls));
        }
        {
            agent_progress::NoProgressGuard g;
            g.record("circuit_run_erc", "{}", erc);
            g.record("circuit_run_erc", "{}", erc);
            g.record("schematic_connect", R"({"a": "U1.V+", "b": "PWR1.1"})", R"({"ok": true})");
            const bool after = g.record("circuit_run_erc", "{}", erc);
            checkTrue("a new result in between resets the count", !after);
        }
        {
            agent_progress::NoProgressGuard g;
            const juce::String fail = R"({"ok": false, "error": "Unknown instance NOPE1."})";
            const bool s = g.record("schematic_connect", R"({"a":"NOPE1.1","b":"R1.1"})", fail) || g.record("schematic_connect", R"({"a":"NOPE1.1","b":"R1.1"})", fail)
                        || g.record("schematic_connect", R"({"a": "NOPE1.1", "b": "R1.1"})", fail);
            checkTrue("identical failing calls (argument spacing aside) stop too", s);
        }
        checkTrue("volatile fields are ignored", agent_progress::normalise(erc) == agent_progress::normalise(ercOtherPath));
    }

    std::printf("-- simulator diagnostics reach the agent --\n");
    {
        // The exact failure from the GPT-4o run: a behavioral source naming a node that does not exist.
        chua_reference::Values v;
        auto bad = chua_reference::core(v);
        bad.netlist.circuit.addBehavioralCurrentSource("B1", bad.v1, 0, "-0.000409*V(NOPE)");
        if (xyce)
        {
            const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_knowledge_tests");
            out.createDirectory();
            const auto r = xyce_backend::run(analytics::Analysis::Transient, { { "outputs", "V(V1)" }, { "stop", "1m" }, { "step", "1u" } }, bad.netlist, out);
            checkTrue("Xyce failure carries Xyce's own message", !r.ok && r.error.containsIgnoreCase("unknown solution node"), r.error.substring(0, 220));
            checkTrue("Xyce failure explains it with the circuit's node names", r.error.contains("Explanation:") && r.error.contains("V1") && r.error.contains("V2"));
            checkTrue("Xyce failure keeps the log path for more detail", r.error.contains("Full log:"));
            checkTrue("Xyce failure stays bounded", r.error.length() < 3000, juce::String(r.error.length()));
        }
        else
            std::printf("SKIP  Xyce not configured: %s\n", detail.toRawUTF8());

        juce::String log;
        for (int i = 0; i < 2000; ++i)
            log << "Processing step " << i << " of the netlist\n";
        log << "Netlist error: Device B1 refers to unknown solution node V1\n";
        for (int i = 0; i < 50; ++i)
            log << "Netlist error in file generated.cir at or near line " << i << ": " << juce::String::repeatedString("x", 600) << "\n";
        log << "Simulation aborted due to error.  There are 0 MSG_FATAL errors and 1 MSG_ERROR errors\n";
        const auto lines = xyce_backend::diagnosticLines(log, 8);
        checkTrue("diagnostics are bounded to the requested lines", lines.size() == 8, juce::String(lines.size()));
        bool shortLines = true;
        for (const auto& l : lines) shortLines = shortLines && l.length() <= 303;
        checkTrue("each diagnostic line is bounded", shortLines);
        checkTrue("original wording kept, first error first", lines[0] == "Netlist error: Device B1 refers to unknown solution node V1", lines[0]);
        checkTrue("the redundant summary line is dropped", !lines.joinIntoString("|").contains("MSG_FATAL"));
        checkTrue("an unmatched log still yields its last lines", !xyce_backend::diagnosticLines("line one\nline two\nthe end", 8).isEmpty());

        const auto internal = analytics::run(analytics::Analysis::Transient, { { "outputs", "V(V1)" }, { "stop", "1m" }, { "step", "1u" } }, bad.netlist);
        checkTrue("internal-solver refusal reaches the result", !internal.ok && internal.error.contains("Xyce"), internal.error);
        auto stuck = chua_reference::opAmpRealization(chua_reference::Values {}); // C1 initial voltage: first step does not converge
        const auto newton = analytics::run(analytics::Analysis::Transient, { { "outputs", "V(V1)" }, { "stop", "1m" }, { "step", "1u" } }, stuck.netlist);
        checkTrue("internal-solver convergence failure reaches the result", !newton.ok && newton.error.containsIgnoreCase("converge"), newton.error);
    }

    std::printf("-- behavioral expression references --\n");
    {
        const auto good = chua_reference::core(chua_reference::Values {});
        checkTrue("existing node accepted", xyce_backend::unresolvedNodes("-0.000409*V(V1)+0.5*(-0.000348)*(abs(V(V1)+1)-abs(V(V1)-1))", good.netlist).isEmpty());
        checkTrue("node names are case-insensitive, ground accepted", xyce_backend::unresolvedNodes("V(v1)-V(0)+V(GND)", good.netlist).isEmpty());
        const auto missing = xyce_backend::unresolvedNodes("V(C1)*(-0.409k)", good.netlist);
        checkTrue("missing node reported", missing.size() == 1 && missing[0] == "C1", missing.joinIntoString(","));
        checkTrue("differential V(a,b) checks both", xyce_backend::unresolvedNodes("V(V1, NOPE)", good.netlist) == juce::StringArray { "NOPE" });
        checkTrue("names ending in v( are not voltage references", xyce_backend::unresolvedNodes("DEV(3)+abs(V(V2))", good.netlist).isEmpty());
        checkTrue("node list uses the netlist's names", xyce_backend::nodeNames(good.netlist).contains("V1") && xyce_backend::nodeNames(good.netlist).contains("V2"));
    }

    std::printf("-- model availability --\n");
    {
        checkTrue("an RLC circuit is writable for Xyce", xyce_backend::netlistProblem(chua_reference::core(chua_reference::Values {}).netlist).isEmpty());
        checkTrue("a behavioral circuit is writable for Xyce", xyce_backend::netlistProblem(chua_reference::behavioralRealization(chua_reference::Values {}).netlist).isEmpty());
        const auto opamp = xyce_backend::netlistProblem(chua_reference::opAmpRealization(chua_reference::Values {}).netlist);
        checkTrue("an op amp without a Xyce model is reported", opamp.containsIgnoreCase("model"), opamp);
        spice_library::initialize();
        checkTrue("library models remain available (UA741)", spice_library::findModel("UA741") != nullptr);
    }

    std::printf("-- task completion protocol --\n");
    {
        using T = agent_progress::CompletionTracker;
        {
            T t;
            checkTrue("a question answered without tools ends normally", t.onTextReply() == T::TextReply::Final);
        }
        {
            T t;
            t.onToolRound();
            t.recordToolResult("analytics_transient", R"({"ok": false, "error": "Xyce failed (exit code 1): Netlist error: Device B1 refers to unknown solution node V1"})");
            checkTrue("'I'll review the connections next' does not end the run", t.onTextReply() == T::TextReply::AskForOutcome);
            checkTrue("open failures are listed for the request", t.openFailures().joinIntoString("").contains("unknown solution node"));
            checkTrue("a second reply with no work in between ends it, unreported", t.onTextReply() == T::TextReply::FinalWithoutOutcome);
        }
        {
            T t;
            t.onToolRound();
            checkTrue("first text after work asks", t.onTextReply() == T::TextReply::AskForOutcome);
            t.onToolRound();
            checkTrue("after more work, text asks again (no fixed cap)", t.onTextReply() == T::TextReply::AskForOutcome);
        }
        {
            T t;
            t.onToolRound();
            t.recordToolResult("circuit_run_erc", R"({"ok": true, "passed": false, "interpretation": "ERC ran and the circuit FAILED with 2 error(s)."})");
            const auto r1 = t.reviewOutcome("completed", "Built it.", "ERC ran");
            checkTrue("completed is pushed back while ERC fails", !r1.accepted && r1.message.contains("circuit_run_erc"), r1.message);
            t.recordToolResult("circuit_run_erc", R"({"ok": true, "passed": true})");
            t.recordToolResult("analytics_transient", R"({"ok": true})");
            checkTrue("completed with evidence and no open failures is accepted", t.reviewOutcome("completed", "Built and simulated.", "ERC passed; double scroll in V(V1) vs V(V2)").accepted);
        }
        {
            T t;
            t.recordToolResult("analytics_transient", R"({"ok": false, "error": "singular matrix"})");
            checkTrue("an explicit failure report ends the run", t.reviewOutcome("failed", "Simulation fails: singular matrix.", "").accepted);
            checkTrue("blocked is accepted", t.reviewOutcome("blocked", "No suitable part.", "").accepted);
            checkTrue("unknown status rejected", !t.reviewOutcome("done", "x", "y").accepted);
            const auto first = t.reviewOutcome("completed", "Looks fine.", "");
            const auto again = t.reviewOutcome("completed", "Looks fine.", "");
            checkTrue("a repeated unverified claim is accepted once, with a caveat (no endless pushback)", !first.accepted && again.accepted && again.message.contains("singular"));
        }
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}



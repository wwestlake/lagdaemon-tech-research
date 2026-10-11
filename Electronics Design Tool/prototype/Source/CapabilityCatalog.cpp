#include "CapabilityCatalog.h"

#include "Analytics.h"
#include "PartCatalog.h"
#include "SchematicSymbols.h"
#include "XyceBackend.h"

#include <algorithm>
#include <map>

namespace capability_catalog
{
namespace
{
using Obj = juce::DynamicObject;

juce::var arrayOf(const juce::StringArray& items)
{
    juce::Array<juce::var> out;
    for (const auto& s : items)
        out.add(s);
    return out;
}

juce::String kindName(parts::Kind kind)
{
    switch (kind)
    {
        case parts::Kind::Quantity: return "quantity";
        case parts::Kind::Text: return "text";
        case parts::Kind::Choice: return "choice";
        case parts::Kind::Toggle: return "toggle";
        case parts::Kind::Fraction: return "fraction";
    }
    return "text";
}

juce::StringArray pinNames(const juce::String& id)
{
    juce::StringArray pins;
    for (const auto& pin : schematic::symbolFor(id).pins)
        pins.add(pin.name);
    return pins;
}

juce::var parameter(const parts::ParamSpec& p)
{
    auto* o = new Obj();
    o->setProperty("key", p.key);
    o->setProperty("label", p.label);
    o->setProperty("kind", kindName(p.kind));
    if (p.unit.isNotEmpty()) o->setProperty("unit", p.unit);
    if (!p.options.isEmpty()) o->setProperty("options", arrayOf(p.options));
    o->setProperty("default", p.defaultValue);
    if (p.help.isNotEmpty()) o->setProperty("help", p.help);
    if (p.showWhen.isNotEmpty()) o->setProperty("appliesWhen", p.showWhen);
    if (p.hidden) o->setProperty("hiddenInPropertiesPane", true);
    return juce::var(o);
}

// Everything a search can match for one component.
juce::String haystack(const juce::String& id)
{
    juce::String text;
    text << id << " " << id.replaceCharacter('_', ' ') << " " << parts::displayName(id) << " "
         << pinNames(id).joinIntoString(" ") << " " << parts::simulationFidelity(id);
    if (schematic::isInstrumentSymbol(id))
        text << " instrument";
    for (const auto& p : parts::paramsFor(id))
        text << " " << p.key << " " << p.label << " " << p.help << " " << p.options.joinIntoString(" ");
    return text.toLowerCase();
}

juce::StringArray words(const juce::String& text)
{
    juce::StringArray out;
    out.addTokens(text.toLowerCase(), " ,;:()[]{}\t\r\n/\\\"'", "");
    out.removeEmptyStrings();
    return out;
}

bool matchesAny(const juce::String& hay, const juce::StringArray& query)
{
    for (const auto& w : query)
        if (hay.contains(w))
            return true;
    return false;
}

juce::String norm(const juce::String& s)
{
    return s.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

// Everyday engineering names for kinds of part, mapped to the words the
// registry's own ids use. Words only: what exists still comes from the
// registry, never from this table.
const std::map<juce::String, juce::StringArray>& synonyms()
{
    static const std::map<juce::String, juce::StringArray> table {
        { "op", { "opamp" } }, { "operationalamplifier", { "opamp" } }, { "amplifier", { "opamp" } },
        { "gnd", { "ground" } }, { "earth", { "ground" } }, { "cap", { "capacitor" } }, { "res", { "resistor" } },
        { "coil", { "inductor" } }, { "choke", { "inductor" } }, { "pot", { "potentiometer" } },
        { "bjt", { "npn", "pnp" } }, { "transistor", { "npn", "pnp", "nmos", "pmos", "njfet", "pjfet" } },
        { "mosfet", { "nmos", "pmos" } }, { "jfet", { "njfet", "pjfet" } },
        { "supply", { "powerport", "powerbus" } }, { "rail", { "powerport", "powerbus" } }, { "power", { "powerport", "powerbus" } },
        { "vcc", { "powerport" } }, { "vdd", { "powerport" } }, { "vee", { "powerport" } }, { "vss", { "powerport" } },
        { "scope", { "oscilloscope" } }, { "meter", { "multimeter" } }, { "dmm", { "multimeter" } },
        { "plot", { "plotter" } }, { "plotter", { "xyzplotter" } }, { "graph", { "plotter" } },
        { "label", { "netlabel" } }, { "vsource", { "voltagesource" } }, { "isource", { "currentsource" } }
    };
    return table;
}

juce::StringArray candidatesFor(const juce::String& term)
{
    juce::StringArray out { term };
    if (const auto found = synonyms().find(term); found != synonyms().end())
        out.addArray(found->second);
    return out;
}

// Query terms: split on spaces, commas and semicolons; op-amp and power_port
// become opamp and powerport; filler words are dropped.
juce::StringArray queryTerms(const juce::String& text)
{
    static const juce::StringArray filler { "and", "or", "with", "the", "a", "an", "of", "for", "component", "components", "part", "parts",
                                            "symbol", "symbols", "device", "devices", "element", "elements" };
    juce::StringArray out;
    for (const auto& token : juce::StringArray::fromTokens(text, " ,;\t\r\n/", ""))
    {
        const auto t = norm(token);
        if (t.isNotEmpty() && !filler.contains(t))
            out.addIfNotAlreadyThere(t);
    }
    return out;
}

// Search match: the term (or one of its synonyms) inside the id or display
// name, or equal to one of their words.
bool termMatches(const juce::String& term, const juce::String& id)
{
    const auto nid = norm(id), nname = norm(parts::displayName(id));
    juce::StringArray words = juce::StringArray::fromTokens(id, "_", "");
    words.addTokens(parts::displayName(id), " -()/", "");
    for (auto& w : words)
        w = norm(w);
    for (const auto& c : candidatesFor(term))
        if ((c.length() >= 3 && (nid.contains(c) || nname.contains(c))) || words.contains(c))
            return true;
    return false;
}

// Strict match, for deciding whether a named part exists: the term or a
// synonym names the part itself (its id, its display name, or its id without
// a qualifier such as _generic or _2ch), not just shares a word with it.
bool namesPart(const juce::String& term, const juce::String& id)
{
    static const juce::StringArray qualifiers { "generic", "2ch", "spst", "spdt", "fixed", "adjustable", "polarized", "bus", "port", "digital", "xyz", "variable" };
    juce::StringArray names { norm(id), norm(parts::displayName(id)) };
    const auto bits = juce::StringArray::fromTokens(id, "_", "");
    if (bits.size() >= 2 && qualifiers.contains(bits[bits.size() - 1]))
        names.add(norm(bits[0]));
    if (bits.size() >= 2 && qualifiers.contains(bits[0]))
        names.add(norm(bits[bits.size() - 1]));
    for (const auto& c : candidatesFor(term))
        if (names.contains(c))
            return true;
    return false;
}

juce::var component(const juce::String& id, bool detail)
{
    auto* o = new Obj();
    o->setProperty("id", id);
    o->setProperty("name", parts::displayName(id));
    o->setProperty("refdesPrefix", schematic::refdesPrefixFor(id));
    o->setProperty("pins", arrayOf(pinNames(id)));
    if (schematic::isInstrumentSymbol(id))
        o->setProperty("instrument", true);
    o->setProperty("simulationFidelity", parts::simulationFidelity(id));
    if (detail)
    {
        juce::Array<juce::var> params;
        for (const auto& p : parts::paramsFor(id))
            params.add(parameter(p));
        o->setProperty("parameters", params);
    }
    else
    {
        juce::StringArray keys;
        for (const auto& p : parts::paramsFor(id))
            keys.add(p.key);
        o->setProperty("parameterKeys", arrayOf(keys));
    }
    return juce::var(o);
}

int editDistance(const juce::String& a, const juce::String& b)
{
    std::vector<int> row((size_t)b.length() + 1);
    for (int j = 0; j <= b.length(); ++j) row[(size_t)j] = j;
    for (int i = 1; i <= a.length(); ++i)
    {
        int prev = row[0];
        row[0] = i;
        for (int j = 1; j <= b.length(); ++j)
        {
            const int cur = row[(size_t)j];
            row[(size_t)j] = std::min({ row[(size_t)j] + 1, row[(size_t)j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1) });
            prev = cur;
        }
    }
    return row[(size_t)b.length()];
}

// Supported ids that share a word with, or are spelled close to, an unknown id.
juce::StringArray closeMatches(const juce::String& unknown)
{
    juce::StringArray out;
    const auto lower = unknown.toLowerCase();
    const auto parts = juce::StringArray::fromTokens(lower, "_- ", "");
    for (const auto& id : schematic::supportedSymbolIds())
    {
        bool shares = false;
        for (const auto& p : parts)
            shares = shares || (p.length() >= 3 && haystack(id).contains(p));
        if (shares || editDistance(lower, id) <= 3)
            out.add(id);
    }
    return out;
}

juce::var analysesList(const juce::StringArray& query)
{
    juce::Array<juce::var> list;
    for (const auto& a : analytics::analyses())
    {
        juce::String hay = a.key + " " + a.title + " " + a.description;
        juce::Array<juce::var> fields;
        for (const auto& f : analytics::fieldsFor(a.id))
        {
            hay << " " << f.key << " " << f.label << " " << f.help;
            auto* fo = new Obj();
            fo->setProperty("key", f.key);
            fo->setProperty("label", f.label);
            if (f.unit.isNotEmpty()) fo->setProperty("unit", f.unit);
            fo->setProperty("default", f.defaultValue);
            if (f.help.isNotEmpty()) fo->setProperty("help", f.help);
            if (!f.options.isEmpty()) fo->setProperty("options", arrayOf(f.options));
            fields.add(juce::var(fo));
        }
        if (!query.isEmpty() && !matchesAny(hay.toLowerCase(), query))
            continue;
        auto* o = new Obj();
        o->setProperty("key", a.key);
        o->setProperty("tool", "analytics_" + a.key);
        o->setProperty("title", a.title);
        o->setProperty("description", a.description);
        if (!query.isEmpty())
            o->setProperty("settings", fields);
        list.add(juce::var(o));
    }
    return list;
}

juce::var engines()
{
    juce::String detail;
    const bool xyce = xyce_backend::isConfigured(detail);
    auto* o = new Obj();
    o->setProperty("internal", "available");
    o->setProperty("xyce", xyce ? juce::String("available") : "not configured: " + detail);
    o->setProperty("note", "Analyses run on the internal solver unless Xyce is chosen; a run that needs the other engine fails with a message saying so.");
    return juce::var(o);
}
}

juce::String componentIdList()
{
    return schematic::supportedSymbolIds().joinIntoString(", ");
}

namespace
{
// What a programmable component is and how the tools make one, built from
// the request's live data (the project's definitions, the node types).
juce::var programmableSection(const Request& request)
{
    auto* o = new Obj();
    o->setProperty("what", "A programmable component is a reusable schematic part whose electrical behaviour is a FRust node program. "
                           "It is backed by a definition in the open project (components/<Name>.frcomp.json with its program "
                           "<Name>.frnode.json): pins, parameters, state variables and output resistance. Each placed instance "
                           "(symbol frust_component, refdes U...) is a copy of that definition with its own state and parameter values; "
                           "changing the definition changes every instance. The library's Programmable Components group lists the "
                           "definitions and New Programmable Component...");
    o->setProperty("incompleteDefinitions", "A definition may have no pins while it is designed: it can be created, saved and placed as a "
                                            "blank symbol, and is not simulated until it has pins. Give it pins with component_update "
                                            "(or the Component panel in the Node Designer, which opens when the user double-clicks an instance).");
    o->setProperty("pinModel", "Every pin is a branch from the pin to its reference (ground, or another pin of the component): current into "
                               "the circuit = I + (V - Vpin_to_ref)/R. role sets the start values: input = no load until the program sets a "
                               "resistance (input impedance); voltage_output = V behind outputResistance (output impedance); current_output "
                               "= a current. Pin names are identifiers (letters, digits, _); wires name them U1.NAME. Each pin also has a "
                               "stable uid (component_get): rename a pin by giving component_update its uid with the new name, and its "
                               "wires stay; a removed pin's wires are removed and reported, never moved to another pin.");
    o->setProperty("programModel", "The program is a node graph (a function, compute()). Component nodes name a pin, parameter or state "
                                   "variable in their text parameter. The internal solver runs the program at every Newton iteration with "
                                   "that iterate's pin voltages, so behaviour that depends on pin voltages (comparators, voltage-dependent "
                                   "resistances, controlled sources) is solved with the circuit. A state read gives the value at the last "
                                   "accepted step; state writes and pin settings are kept when the step is accepted. Outside a simulation "
                                   "pin reads are 0. Ordinary nodes (arithmetic, comparisons gt/lt, if, literal_f64...) build the logic: "
                                   "node_program_node_types lists every node type with its pins. There is no boolean and/or node: "
                                   "combine conditions by chaining if nodes, one if's value feeding the then or else input of the next "
                                   "(a value inside a range: if below the low limit then A, else (if above the high limit then A, "
                                   "else B)). Every condition the requirement names must appear in the graph.");
    if (request.componentNodeTypes.isArray())
        o->setProperty("componentNodeTypes", request.componentNodeTypes);
    o->setProperty("simulation", "Programmable components run on the internal solver (operating point, DC sweep, transient: the analytics_* "
                                 "tools); Xyce refuses circuits that contain one. Simulations compile changed programs automatically; "
                                 "component_compile checks a program first and returns its FRust source or the error.");
    juce::Array<juce::var> tools;
    auto tool = [&tools](const char* name, const char* use) {
        auto* t = new Obj();
        t->setProperty("tool", name);
        t->setProperty("use", use);
        tools.add(juce::var(t));
    };
    tool("component_list", "the project's definitions, pin counts and instances: look here first");
    tool("component_create", "a new definition (name, pins, parameters, state); no name and no pins makes a blank one");
    tool("component_get", "read a definition: pins with uids, roles and layout, parameters, state, instances");
    tool("component_update", "change pins (keep a pin's uid to rename it), parameters, state, description, output resistance");
    tool("component_rename", "rename a definition; its program and instances (every diagram) follow");
    tool("component_open_program", "open its program in the Node Designer for the node_program_* tools");
    tool("node_program_node_types / node_program_add_node / node_program_connect / node_program_set_parameter / node_program_inspect / node_program_validate",
         "build and check the behaviour graph (component nodes name pins, parameters, state in their text parameter)");
    tool("component_compile", "compile it; returns the FRust source or the error");
    tool("component_place", "place an instance at x, y (no name: a new blank component)");
    tool("component_set_parameter", "one instance's parameter value");
    tool("pin_layout_get / pin_layout_set", "where pins are drawn (side, order); belongs to the definition");
    tool("schematic_connect", "wire its pins (U1.NAME) into the circuit");
    tool("analytics_operating_point / analytics_transient / instrument_read", "simulate and read the results");
    o->setProperty("tools", tools);
    o->setProperty("workflow", juce::StringArray { "1. component_list: is there a definition to reuse?",
                                                   "2. component_create (or component_update an existing one): name, pins with roles, parameters, state.",
                                                   "3. component_open_program, then node_program_* to build the behaviour: component nodes read pins, time, parameters and state, and drive pins.",
                                                   "4. node_program_validate and component_compile; fix what they report.",
                                                   "5. component_place the instance; schematic_connect its pins into a test circuit.",
                                                   "6. Simulate (analytics_operating_point / analytics_transient) and compare every result with the value the requirement asks for at that input.",
                                                   "7. If any result differs from the requirement, it is not done: change the program or definition, compile, simulate again. Report success only when every checked value matches.",
                                                   "For the user to finish by hand: component_place with no name places a blank component; they double-click it to design it." });
    juce::Array<juce::var> defs;
    for (const auto& d : request.programmable)
    {
        auto* e = new Obj();
        e->setProperty("name", d.name);
        e->setProperty("pinCount", d.pins.size());
        e->setProperty("pins", arrayOf(d.pins));
        e->setProperty("instances", arrayOf(d.instances));
        defs.add(juce::var(e));
    }
    o->setProperty("definitions", defs);
    o->setProperty("definitionsNote", !request.projectOpen ? juce::String("No project is open: definitions live in a project (project_create or project_open first).")
                                      : request.programmable.empty() ? juce::String("The open project has no definitions yet.")
                                      : juce::String(request.programmable.size()) + " definition(s) in the open project.");
    return juce::var(o);
}

// Logic gates: what they are electrically, how they time, how to test them.
juce::var logicSection()
{
    auto* o = new Obj();
    o->setProperty("what", "Logic gates are electrical parts simulated by the internal solver: NOT and BUFFER (1 input), AND, NAND, OR, NOR, "
                           "XOR (odd parity) and XNOR (2..8 inputs, parameter inputs). Inputs A, B, C... on the left, output Y on the right. "
                           "Every gate uses one shared logical function; there is no separate digital simulator.");
    juce::Array<juce::var> gates;
    for (const auto* id : { "logic_not", "logic_buffer", "logic_and", "logic_nand", "logic_or", "logic_nor", "logic_xor", "logic_xnor" })
    {
        auto* g = new Obj();
        g->setProperty("symbolId", id);
        g->setProperty("name", parts::displayName(id));
        g->setProperty("inputs", juce::String(schematic::defaultGateInputs(id))
                                     + (schematic::maxGateInputs(id) > schematic::defaultGateInputs(id) ? "..8 (parameter inputs, default 2)" : ""));
        gates.add(juce::var(g));
    }
    o->setProperty("gates", gates);
    o->setProperty("electricalModel",
                   "First-pass model (identified as such): no supply pins; the output is a level (vol LOW, voh HIGH) behind rout to ground, "
                   "so it drives and sags into loads like a real output; each input is rin to ground and switches at threshold, with "
                   "hysteresis (rising at threshold + h/2, falling at threshold - h/2). Defaults: a 5 V CMOS-like gate (voh 5 V, vol 0 V, "
                   "threshold 2.5 V, hysteresis 0, rout 50 ohm, rin 1 Gohm, tpd 10 ns, tr 4 ns). Not modelled: supply current, output "
                   "sag with a supply, input capacitance, temperature.");
    o->setProperty("dc", "Operating point, DC sweep and AC bias: the output follows the inputs with no delay through a smooth transfer "
                         "curve (gain 25 V/V at the threshold): rail levels for inputs clear of the threshold, a mid level for an input "
                         "held at it. A truth table is an operating point per input combination.");
    o->setProperty("timing", "Transient: an input crossing its switching point (time interpolated in the step) that changes the gate's "
                             "result starts the output change exactly tpd later (the solver lands a step on that time), and the output "
                             "moves linearly to the new level over tr. Delay semantics: inertial - a new input change cancels a change "
                             "not yet started, so input pulses shorter than tpd do not reach the output. tpd must be > 0.");
    o->setProperty("feedback", "Gates may be wired in loops (latches, ring oscillators, Schmitt RC oscillators): every change waits tpd, "
                               "so a loop advances in time and never loops without time passing. A latch whose inputs are both inactive at "
                               "t = 0 has no defined state (its DC solution is the mid level, and equal delays then oscillate); start it "
                               "with set or reset asserted. An odd ring of inverters oscillates (period 2 x stages x (tpd + tr/2)).");
    o->setProperty("sequential", "There are no flip-flop or counter parts: build them from gates. SR latch: two NANDs cross-coupled "
                                 "(Q = NAND(S', Q'), Q' = NAND(R', Q), set and reset active low) or two NORs (active high). Clocked D "
                                 "latch: NOT on D, NAND(D, CLK) and NAND(D', CLK) driving a NAND SR latch. Drive clocks and input "
                                 "sequences with signal_source (waveform Pulse, Square or PWL).");
    o->setProperty("simulation", "Internal solver only (analytics_operating_point, analytics_dc_sweep, analytics_transient). With the "
                                 "Xyce engine selected the run falls back to the internal solver and the result says so; there is no "
                                 "Xyce co-simulation. Gates work with every other part, programmable components included.");
    o->setProperty("workflow", juce::StringArray {
        "1. schematic_place_symbol with the gate's symbolId (workbench_capabilities symbolId gives its pins and parameters).",
        "2. schematic_set_parameters for inputs (2..8), levels, threshold, hysteresis, rout, tpd, tr; lowering inputs removes wires on removed inputs (reported).",
        "3. schematic_connect the inputs (U1.A, U1.B...) and the output (U1.Y); an unwired input reads LOW (the result warns).",
        "4. Truth table: a voltage_source (0 V or the HIGH level) on each input, analytics_operating_point per combination, read Y.",
        "5. Timing, latches, clocks: signal_source inputs (Pulse/PWL), analytics_transient, read Y over time (edges are sampled at their exact times).",
        "6. Compare every result with the expected logic level and timing; a mismatch means the circuit is not done." });
    o->setProperty("limitations", "No supply pins or supply current; no digital-only simulation mode; no logic analyzer; no flip-flop, "
                                  "counter or register parts; PCB footprints only for single 1- and 2-input gates.");
    return juce::var(o);
}

bool asksAboutLogic(const juce::StringArray& query)
{
    static const juce::StringArray topics { "logic", "gate", "gates", "nand", "nor", "xor", "xnor", "inverter", "buffer", "latch",
                                            "flipflop", "digital", "boolean", "truth", "truthtable", "propagation", "clock", "sr" };
    for (const auto& t : query)
        if (topics.contains(t))
            return true;
    return false;
}

bool asksAboutProgrammable(const juce::StringArray& query)
{
    static const juce::StringArray topics { "programmable", "frust", "frustcomponent", "custom", "behavioral", "behavioural", "nodeprogram",
                                            "microcontroller", "mcu", "controller", "program", "programmed" };
    for (const auto& t : query)
        if (topics.contains(t))
            return true;
    return false;
}
}

juce::String toJson(const Request& request)
{
    auto* root = new Obj();
    root->setProperty("ok", true);
    root->setProperty("tool", "workbench_capabilities");
    root->setProperty("source", "Live application registries (symbol table, part parameter catalog, analysis table).");
    root->setProperty("registryComponentCount", schematic::supportedSymbolIds().size());
    const auto section = request.section.trim().toLowerCase();
    const auto query = queryTerms(request.query);

    if (request.symbolId.trim().isNotEmpty())
    {
        const auto id = request.symbolId.trim();
        if (!schematic::isSupportedSymbol(id))
        {
            root->setProperty("ok", false);
            root->setProperty("error", "No component type '" + id + "' exists in this Workbench. Do not place or reference it.");
            root->setProperty("closeMatches", arrayOf(closeMatches(id)));
            return juce::JSON::toString(juce::var(root), true);
        }
        root->setProperty("component", component(id, true));
        if (id == "frust_component")
            root->setProperty("programmableComponents", programmableSection(request));
        if (schematic::isLogicGate(id))
            root->setProperty("logicGates", logicSection());
        return juce::JSON::toString(juce::var(root), true);
    }

    const bool all = section.isEmpty();
    if (all || section == "components" || section == "instruments")
    {
        // Each query term is matched on its own; a part matching any term is
        // listed, parts matching more terms first, each with the terms it matched.
        struct Hit { juce::String id; juce::StringArray terms; };
        std::vector<Hit> hits;
        std::map<juce::String, juce::StringArray> byTerm;
        std::map<juce::String, int> exactCount, qualifiedCount;
        for (const auto& id : schematic::supportedSymbolIds())
        {
            Hit hit { id, {} };
            for (const auto& term : query)
                if (termMatches(term, id))
                {
                    hit.terms.add(term);
                    // The part the term names outright (ground -> ground) leads that
                    // term's list, then parts it names with a qualifier (ground_bus).
                    const bool outright = norm(id) == term || norm(parts::displayName(id)) == term;
                    if (outright)
                        byTerm[term].insert(exactCount[term]++, id);
                    else if (namesPart(term, id))
                        byTerm[term].insert(exactCount[term] + qualifiedCount[term]++, id);
                    else
                        byTerm[term].add(id);
                }
            if (query.isEmpty() || !hit.terms.isEmpty())
                hits.push_back(hit);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.terms.size() > b.terms.size(); });
        juce::Array<juce::var> comps, instruments;
        for (const auto& hit : hits)
        {
            const bool instrument = schematic::isInstrumentSymbol(hit.id);
            if (section == "instruments" && !instrument)
                continue;
            auto entry = component(hit.id, !query.isEmpty() || instrument);
            if (!hit.terms.isEmpty())
                entry.getDynamicObject()->setProperty("matchedTerms", arrayOf(hit.terms));
            (instrument ? instruments : comps).add(entry);
        }
        if (!query.isEmpty())
        {
            juce::Array<juce::var> terms;
            juce::StringArray unmatched;
            for (const auto& term : query)
            {
                auto* t = new Obj();
                t->setProperty("term", term);
                t->setProperty("matches", arrayOf(byTerm[term]));
                if (byTerm[term].isEmpty())
                {
                    t->setProperty("closeMatches", arrayOf(closeMatches(term)));
                    unmatched.add(term);
                }
                terms.add(juce::var(t));
            }
            root->setProperty("queryTerms", terms);
            root->setProperty("searchRule", "Each term is matched separately against component ids, display names and common synonyms; a component matching any term is listed, most matched terms first.");
            if (!unmatched.isEmpty())
                root->setProperty("note", "No component matched: " + unmatched.joinIntoString(", ") + ". An empty search does not show a part is absent: "
                                          "call workbench_capabilities with no query to list all " + juce::String(schematic::supportedSymbolIds().size())
                                          + " registered components, or look one up by symbolId, before concluding anything is missing. "
                                            "A part the registry does not have can often be built as a programmable component: see programmableComponents.");
        }
        if (all || section == "components")
            root->setProperty("components", comps);
        if (all || section == "instruments")
        {
            root->setProperty("instruments", instruments);
            root->setProperty("instrumentTools", "Place instruments with schematic_place_symbol and wire their pins like any part (they add no load). "
                                                 "Read them with instrument_read; plotter samples with instrument_plot_data; change settings with schematic_set_parameters.");
        }
    }
    // Programmable components: in the summary, when asked about, and when a
    // searched part is not in the registry (it may be built as one).
    bool unmatchedTerm = false;
    for (const auto& term : query)
    {
        bool any = false;
        for (const auto& id : schematic::supportedSymbolIds())
            any |= termMatches(term, id);
        unmatchedTerm |= !any;
    }
    if (all || section == "programmable" || asksAboutProgrammable(query) || unmatchedTerm)
        root->setProperty("programmableComponents", programmableSection(request));
    if (all || section == "logic" || asksAboutLogic(query))
        root->setProperty("logicGates", logicSection());
    if (all || section == "analyses")
        root->setProperty("analyses", analysesList(section == "analyses" ? query : juce::StringArray()));
    if (all || section == "engines")
        root->setProperty("engines", engines());
    return juce::JSON::toString(juce::var(root), true);
}

std::vector<TermResolution> resolve(const juce::StringArray& names)
{
    std::vector<TermResolution> out;
    for (const auto& name : names)
    {
        TermResolution r { name, {} };
        const auto term = norm(name);
        if (term.isNotEmpty())
            for (const auto& id : schematic::supportedSymbolIds())
                if (namesPart(term, id))
                    r.ids.add(id);
        out.push_back(r);
    }
    return out;
}

juce::String checkGapClaim(const juce::String& category, const juce::String& description, const juce::String& neededCapability,
                           const juce::StringArray& components)
{
    juce::StringArray claimed = components;
    const auto cat = category.toLowerCase();
    if (claimed.isEmpty() && (cat.contains("component") || cat.contains("part") || cat.contains("library")))
    {
        // Words and adjacent word pairs ("voltage source") of the claim.
        const auto tokens = juce::StringArray::fromTokens(description + " " + neededCapability, " ,;:.()[]\t\r\n/'\"", "");
        for (int i = 0; i < tokens.size(); ++i)
        {
            claimed.add(tokens[i]);
            if (i + 1 < tokens.size())
                claimed.add(tokens[i] + " " + tokens[i + 1]);
        }
    }
    juce::StringArray existing;
    for (const auto& r : resolve(claimed))
        if (!r.ids.isEmpty())
            existing.addIfNotAlreadyThere(r.name.trim() + " -> " + r.ids.joinIntoString(", "));
    if (existing.isEmpty())
        return {};
    return "Not recorded: the registry has these components: " + existing.joinIntoString("; ")
         + ". A search that returned nothing is not evidence that a part is missing; use these ids (workbench_capabilities symbolId gives pins and parameters). "
           "If the gap is something more specific (a particular model or part), name exactly that in components.";
}
}

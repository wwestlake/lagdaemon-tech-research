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
                                          + " registered components, or look one up by symbolId, before concluding anything is missing.");
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

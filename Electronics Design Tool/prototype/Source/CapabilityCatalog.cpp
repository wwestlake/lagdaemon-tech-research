#include "CapabilityCatalog.h"

#include "Analytics.h"
#include "PartCatalog.h"
#include "SchematicSymbols.h"
#include "XyceBackend.h"

#include <algorithm>

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

bool matchesAll(const juce::String& hay, const juce::StringArray& query)
{
    for (const auto& w : query)
        if (!hay.contains(w))
            return false;
    return true;
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
        if (!query.isEmpty() && !matchesAll(hay.toLowerCase(), query))
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
    root->setProperty("source", "Live application registries (symbol table, part parameter catalog, analysis table). Only what is listed exists.");
    const auto section = request.section.trim().toLowerCase();
    const auto query = words(request.query);

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
        juce::Array<juce::var> comps, instruments;
        for (const auto& id : schematic::supportedSymbolIds())
        {
            if (!query.isEmpty() && !matchesAll(haystack(id), query))
                continue;
            const bool instrument = schematic::isInstrumentSymbol(id);
            const bool detail = !query.isEmpty() || instrument;
            (instrument ? instruments : comps).add(component(id, detail));
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
    if (!query.isEmpty())
    {
        const auto comps = root->getProperty("components");
        const auto inst = root->getProperty("instruments");
        const auto found = (comps.isArray() ? comps.size() : 0) + (inst.isArray() ? inst.size() : 0);
        if (found == 0 && section != "analyses" && section != "engines")
            root->setProperty("note", "Nothing matches '" + request.query + "'. The Workbench has no such component; say so rather than inventing one.");
    }
    return juce::JSON::toString(juce::var(root), true);
}
}

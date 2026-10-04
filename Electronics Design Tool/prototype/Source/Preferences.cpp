#include "Preferences.h"

#include <map>

namespace prefs
{
namespace
{
Setting toggle(juce::String key, juce::String category, juce::String label, juce::String description, bool on)
{
    return { key, category, label, description, Kind::Toggle, {}, on ? "true" : "false" };
}

Setting choice(juce::String key, juce::String category, juce::String label, juce::String description,
               juce::StringArray options, juce::String def)
{
    return { key, category, label, description, Kind::Choice, options, def };
}

std::vector<Setting> build()
{
    const auto defaultProjects = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                     .getChildFile("Djehuti Electronics Lab").getChildFile("Projects").getFullPathName();
    return {
        // Layout
        toggle("layout.supply_symbols", "Layout", "Use supply ports and ground symbols for rails",
               "Auto layout draws every supply and ground as a symbol at each pin. Off: rails you placed stay rails and are re-tapped to their pins.", true),
        toggle("layout.instrument_labels", "Layout", "Connect instruments with net labels",
               "Scope, meter and analyzer inputs connect through matching labels (SCOPE1.CH1) instead of wires across the drawing.", true),
        toggle("layout.stack_vertical_chains", "Layout", "Stack parts in line with transistor pins",
               "A part fed only by a transistor's emitter, collector, source or drain stands in line with that pin (the textbook push-pull column).", true),
        toggle("layout.supply_block", "Layout", "Put DC supplies in a separate block",
               "Supply sources go in a block under the circuit. Off: they stand in a column on the left of the circuit.", true),
        choice("layout.spacing", "Layout", "Spacing", "How much room auto layout leaves between parts and columns.",
               { "Compact", "Normal", "Roomy" }, "Normal"),
        choice("layout.wire_style", "Layout", "Wire style", "Fewest bends keeps wires straight even if a little longer; shortest wires takes more corners.",
               { "Fewest bends", "Shortest wires" }, "Fewest bends"),
        choice("layout.wire_gap", "Layout", "Gap between parallel wires", "Space between neighbouring wires of different nets.",
               { "1 grid step", "2 grid steps" }, "1 grid step"),
        toggle("layout.after_design_tools", "Layout", "Auto layout after a design tool builds a circuit",
               "Run auto layout when a design tool (push-pull amplifier, high-pass filter) finishes drawing.", true),
        toggle("layout.subdiagram_inner_layout", "Layout", "Auto layout a new sub-diagram's sheet",
               "When parts are folded into a block, lay out the block's own sheet.", true),
        toggle("layout.relayout_after_expand", "Layout", "Re-lay out after expanding a block",
               "When a block is expanded back onto its sheet, run auto layout on that sheet.", false),

        // Display
        choice("display.pin_names", "Display", "Show pin names", "Auto shows them where the symbol does not make them obvious (instruments, transformers, blocks).",
               { "Auto", "Always", "Never" }, "Auto"),
        toggle("display.show_values", "Display", "Show part values", "Draw each part's value (10k, 100n, 1N4148) beside it.", true),
        toggle("display.show_refdes", "Display", "Show reference designators", "Draw each part's reference designator (R1, Q2) beside it.", true),
        choice("display.label_size", "Display", "Label text size", "Size of reference designators and values on the schematic.",
               { "Small", "Normal", "Large" }, "Normal"),
        choice("display.grid", "Display", "Grid", "How the schematic grid is drawn.", { "Dots", "Lines", "Hidden" }, "Lines"),
        toggle("display.snap_default", "Display", "Snap to grid", "Parts, wires and junctions snap to the 24 px grid.", true),

        // Units
        choice("units.capital_m", "Units", "Capital M means", "SPICE reads M as milli and MEG as mega. Mega reads M as mega (1M = one million); MEG always means mega.",
               { "Mega", "Milli (SPICE)" }, "Mega"),
        choice("units.ohm_symbol", "Units", "Resistance unit", "Show resistance with the ohm sign or the word ohm.", { juce::String::fromUTF8("\xce\xa9"), "ohm" },
               juce::String::fromUTF8("\xce\xa9")),

        // Projects
        { "projects.default_folder", "Projects", "Default projects folder", "Where New Project suggests storing projects.",
          Kind::Folder, {}, defaultProjects },
        toggle("projects.reopen_last", "Projects", "Reopen the last project on start", "Open the most recent project and its last diagram when the app starts.", true),
    };
}

std::map<juce::String, juce::String>& values()
{
    static std::map<juce::String, juce::String> v;
    return v;
}

juce::File file()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab").getChildFile("preferences.json");
}

bool& loaded()
{
    static bool flag = false;
    return flag;
}

void ensureLoaded()
{
    if (loaded())
        return;
    loaded() = true;
    const auto parsed = juce::JSON::parse(file().loadFileAsString());
    if (auto* object = parsed.getDynamicObject())
        for (const auto& property : object->getProperties())
            if (find(property.name.toString()) != nullptr)
                values()[property.name.toString()] = property.value.toString();
}

void save()
{
    auto* object = new juce::DynamicObject();
    for (const auto& [key, value] : values())
        object->setProperty(key, value);
    file().getParentDirectory().createDirectory();
    file().replaceWithText(juce::JSON::toString(juce::var(object), false));
}

std::map<int, std::function<void(const juce::String&)>>& listeners()
{
    static std::map<int, std::function<void(const juce::String&)>> l;
    return l;
}
}

const std::vector<Setting>& all()
{
    static const auto settings = build();
    return settings;
}

const Setting* find(const juce::String& key)
{
    for (const auto& s : all())
        if (s.key == key)
            return &s;
    return nullptr;
}

juce::StringArray categories()
{
    juce::StringArray result;
    for (const auto& s : all())
        result.addIfNotAlreadyThere(s.category);
    return result;
}

juce::String get(const juce::String& key)
{
    ensureLoaded();
    const auto found = values().find(key);
    if (found != values().end())
        return found->second;
    const auto* s = find(key);
    return s != nullptr ? s->defaultValue : juce::String();
}

bool isOn(const juce::String& key)
{
    return get(key) == "true";
}

bool set(const juce::String& key, const juce::String& value, juce::String& error)
{
    ensureLoaded();
    const auto* s = find(key);
    if (s == nullptr)
    {
        error = "No preference \"" + key + "\".";
        return false;
    }
    auto v = value.trim();
    switch (s->kind)
    {
        case Kind::Toggle:
            if (v.equalsIgnoreCase("on") || v.equalsIgnoreCase("yes") || v == "1") v = "true";
            if (v.equalsIgnoreCase("off") || v.equalsIgnoreCase("no") || v == "0") v = "false";
            v = v.toLowerCase();
            if (v != "true" && v != "false")
            {
                error = s->label + " is on or off (true/false).";
                return false;
            }
            break;
        case Kind::Choice:
        {
            const auto index = s->options.indexOf(v, true);
            if (index < 0)
            {
                error = s->label + ": choose one of " + s->options.joinIntoString(", ") + ".";
                return false;
            }
            v = s->options[index];
            break;
        }
        case Kind::Folder:
            if (v.isEmpty() || !juce::File::isAbsolutePath(v))
            {
                error = s->label + " must be a full folder path.";
                return false;
            }
            break;
    }
    values()[key] = v;
    save();
    for (const auto& [id, listener] : listeners())
        if (listener)
            listener(key);
    return true;
}

int addListener(std::function<void(const juce::String&)> listener)
{
    static int next = 1;
    listeners()[next] = std::move(listener);
    return next++;
}

void removeListener(int id)
{
    listeners().erase(id);
}
}

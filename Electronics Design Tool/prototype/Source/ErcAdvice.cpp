#include "ErcAdvice.h"

namespace erc_advice
{
int supplyPolarity(const juce::String& pinName)
{
    const auto p = pinName.trim().toUpperCase();
    if (p == "V+" || p == "VCC" || p == "VDD" || p == "VS+")
        return 1;
    if (p == "V-" || p == "VEE" || p == "VSS" || p == "VS-")
        return -1;
    return 0;
}

void suggest(Finding& f, const Context& context)
{
    auto& s = f.suggestions;
    const auto label = f.refdes + "." + f.pin;
    const auto ground = context.groundNets.isEmpty() ? juce::String("ground") : context.groundNets[0];
    if (f.category == "unconnected_supply_pin")
    {
        const auto positive = supplyPolarity(f.pin) > 0;
        juce::StringArray candidates;
        for (const auto& net : context.supplyNets)
            if (net.startsWithChar('-') != positive)
                candidates.add(net);
        if (!candidates.isEmpty())
            s.add("Connect " + label + " to an existing " + (positive ? "positive" : "negative") + " supply net (" + candidates.joinIntoString(", ")
                  + ") with schematic_connect, choosing the one the part is rated for.");
        s.add("Or create the rail: schematic_place_symbol power_port with busName set to the " + juce::String(positive ? "positive" : "negative")
              + " supply voltage this part needs (" + (positive ? "for example +12V" : "for example -12V")
              + "; take it from the part's rating and the signal swing the circuit needs), then schematic_connect the port's pin 1 to " + label + ".");
        s.add("A power_port only names the net: drive each new rail with a voltage_source (+ to the rail, - to " + ground + ", value = rail voltage) "
              "or ERC reports the supply net as undriven.");
        return;
    }
    if (f.category == "unconnected_pin")
    {
        s.add("schematic_connect " + label + " to the net it belongs on (see this part's role in the design), or schematic_delete_components "
              + f.refdes + " if it is not needed.");
        return;
    }
    if (f.category == "no_ground")
    {
        s.add("schematic_place_symbol ground, then schematic_connect its pin 0 to the circuit's reference node (for example a source's - pin).");
        return;
    }
    if (f.category == "undriven_supply_net")
    {
        s.add("schematic_place_symbol voltage_source with value set to the rail voltage, schematic_connect its + pin to a port on " + f.net
              + " (or the port's pin 1) and its - pin to " + ground + ".");
        return;
    }
    if (f.category == "shorted_source")
    {
        s.add("Both terminals of " + f.refdes + " are on " + f.net + ": move one with schematic_connect to the intended node; "
              "if a wire was made by mistake, rebuild the connection (schematic_delete_components and place again if needed).");
        return;
    }
    if (f.category == "shorted_part")
    {
        s.add(f.refdes + " has both pins on " + f.net + " so it does nothing: reconnect one pin with schematic_connect to the node it should reach.");
        return;
    }
    if (f.category == "invalid_value" || f.category == "missing_value")
    {
        s.add("schematic_set_parameters " + f.refdes + " with a valid value (workbench_capabilities symbolId " + f.symbolId
              + " lists its parameters, units and choices).");
        return;
    }
    if (f.category == "duplicate_refdes")
    {
        s.add("schematic_rename_component one of the parts named " + f.refdes + " to a unique reference designator.");
        return;
    }
    if (f.category == "unnamed_net_label" || f.category == "unnamed_supply_port")
    {
        s.add("schematic_set_parameters " + f.refdes + " busName to the net name it should join.");
        return;
    }
    if (f.category == "unresolved_expression_node")
    {
        s.add("Write V(...) with one of this circuit's node names: " + (context.nodeNames.isEmpty() ? juce::String("(none yet)") : context.nodeNames.joinIntoString(", "))
              + "; change the expression with schematic_set_parameters " + f.refdes + " value.");
        s.add("Or give the node you mean the name " + f.net.upToFirstOccurrenceOf(",", false, false)
              + ": schematic_place_symbol net_label with that busName and schematic_connect its pin 1 to the node.");
        return;
    }
    if (f.category == "unknown_model")
    {
        s.add("schematic_set_parameters " + f.refdes + " value to a model the library has, or back to the part's default (workbench_capabilities symbolId "
              + f.symbolId + " shows it).");
        return;
    }
    if (f.category == "xyce_unavailable")
    {
        s.add("Simulate on the internal solver (it has built-in models for these parts), or replace the part with one the Xyce engine supports "
              "(workbench_capabilities lists each part's simulationFidelity).");
        return;
    }
    if (f.category == "not_simulated")
    {
        s.add("Expect " + f.refdes + " to be absent from Xyce runs; use a simulated part or the internal solver if it matters.");
        return;
    }
}

juce::var toVar(const Finding& f)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("severity", f.severity);
    o->setProperty("category", f.category);
    o->setProperty("message", f.message);
    if (f.refdes.isNotEmpty())
    {
        o->setProperty("refdes", f.refdes);
        o->setProperty("componentId", f.refdes);
    }
    if (f.symbolId.isNotEmpty()) o->setProperty("symbolId", f.symbolId);
    if (f.pin.isNotEmpty())
    {
        o->setProperty("pin", f.pin);
        o->setProperty("pinLabel", f.refdes + "." + f.pin);
        o->setProperty("pinIndex", f.pinIndex);
    }
    if (f.net.isNotEmpty()) o->setProperty("net", f.net);
    if (!f.connections.isEmpty())
    {
        juce::Array<juce::var> c;
        for (const auto& x : f.connections) c.add(x);
        o->setProperty("existingConnections", c);
    }
    juce::Array<juce::var> fixes;
    for (const auto& x : f.suggestions) fixes.add(x);
    o->setProperty("suggestedActions", fixes);
    return juce::var(o);
}

juce::String markdownLine(const Finding& f)
{
    juce::String line;
    line << "- [" << f.severity << "] " << f.message;
    if (!f.suggestions.isEmpty())
        line << " Fix: " << f.suggestions[0];
    return line << "\n";
}
}

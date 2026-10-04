#include "PartCatalog.h"

#include "CircuitSolver.h"

#include <map>

namespace parts
{
namespace
{
ParamSpec quantity(juce::String key, juce::String label, juce::String unit, juce::String def,
                   Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Quantity, unit, {}, def, storage, help };
}

ParamSpec text(juce::String key, juce::String label, juce::String def, Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Text, {}, {}, def, storage, help };
}

ParamSpec choice(juce::String key, juce::String label, juce::StringArray options, juce::String def,
                 Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Choice, {}, options, def, storage, help };
}

ParamSpec toggle(juce::String key, juce::String label, juce::String off, juce::String on, juce::String def, juce::String help = {})
{
    return { key, label, Kind::Toggle, {}, { off, on }, def, Storage::Param, help };
}

ParamSpec fraction(juce::String key, juce::String label, juce::String def, juce::String help = {})
{
    return { key, label, Kind::Fraction, {}, {}, def, Storage::Param, help };
}

std::vector<ParamSpec> waveformSource(const juce::String& unit)
{
    return {
        choice("waveform", "Waveform", { "Sine", "Square" }, "Sine"),
        quantity("amplitude", "Amplitude (peak)", unit, unit == "A" ? "1m" : "1", Storage::Value),
        quantity("frequency", "Frequency", "Hz", "1k", Storage::Frequency),
        quantity("offset", "DC offset", unit, "0"),
        quantity("phase", "Phase", "deg", "0"),
        quantity("duty", "Duty cycle (square)", "", "0.5"),
    };
}

std::map<juce::String, std::vector<ParamSpec>> buildCatalog()
{
    std::map<juce::String, std::vector<ParamSpec>> c;
    c["resistor"] = { quantity("value", "Resistance", "ohm", "10k", Storage::Value) };
    c["potentiometer"] = { quantity("value", "Total resistance", "ohm", "10k", Storage::Value),
                           fraction("position", "Wiper position", "0.5", "0 = pin 1 end, 1 = pin 2 end") };
    c["capacitor"] = { quantity("value", "Capacitance", "F", "1u", Storage::Value) };
    c["capacitor_polarized"] = { quantity("value", "Capacitance", "F", "10u", Storage::Value) };
    c["variable_capacitor"] = { quantity("value", "Capacitance", "F", "100p", Storage::Value) };
    c["inductor"] = { quantity("value", "Inductance", "H", "10m", Storage::Value) };
    c["coupled_inductor"] = { quantity("value", "Inductance (each)", "H", "10m", Storage::Value),
                              quantity("coupling", "Coupling k", "", "0.99", Storage::Param, "0 to 1") };
    c["transformer"] = { text("value", "Turns ratio (primary:secondary)", "1:1", Storage::Value),
                         quantity("primary_inductance", "Primary inductance", "H", "1"),
                         quantity("coupling", "Coupling k", "", "0.999") };
    c["diode"] = { text("value", "Model", "1N4148", Storage::Value),
                   quantity("saturation_current", "Saturation current Is", "A", "2.52n"),
                   quantity("emission", "Emission coefficient n", "", "1.752") };
    c["zener_diode"] = { quantity("value", "Zener voltage", "V", "5.1", Storage::Value),
                         quantity("saturation_current", "Saturation current Is", "A", "1n"),
                         quantity("emission", "Emission coefficient n", "", "1.5") };
    c["schottky_diode"] = { text("value", "Model", "BAT54", Storage::Value),
                            quantity("saturation_current", "Saturation current Is", "A", "100n"),
                            quantity("emission", "Emission coefficient n", "", "1.05") };
    c["led"] = { choice("value", "Colour", { "Red", "Green", "Yellow", "Blue", "White" }, "Red", Storage::Value) };
    c["battery"] = { quantity("value", "Voltage", "V", "9", Storage::Value) };
    c["voltage_source"] = { quantity("value", "Voltage", "V", "5", Storage::Value) };
    c["current_source"] = { quantity("value", "Current", "A", "1m", Storage::Value) };
    c["ac_voltage_source"] = waveformSource("V");
    c["signal_source"] = waveformSource("V");
    c["ac_current_source"] = waveformSource("A");
    c["vcvs"] = { quantity("value", "Voltage gain", "V/V", "10", Storage::Value) };
    c["vccs"] = { quantity("value", "Transconductance", "A/V", "1m", Storage::Value) };
    c["ccvs"] = { quantity("value", "Transresistance", "V/A", "1k", Storage::Value) };
    c["cccs"] = { quantity("value", "Current gain", "A/A", "10", Storage::Value) };
    c["opamp_741"] = { text("value", "Model", "uA741", Storage::Value),
                       quantity("gain", "Open-loop gain", "V/V", "200k"),
                       quantity("headroom", "Output headroom from rails", "V", "1.5") };
    for (const auto* id : { "npn", "pnp" })
        c[id] = { text("value", "Model", juce::String(id) == "npn" ? "generic_npn" : "generic_pnp", Storage::Value),
                  quantity("beta", "Current gain (beta)", "", "100"),
                  quantity("saturation_current", "Saturation current Is", "A", "10f") };
    for (const auto* id : { "nmos", "pmos" })
        c[id] = { text("value", "Model", juce::String(id) == "nmos" ? "generic_nmos" : "generic_pmos", Storage::Value),
                  quantity("threshold", "Threshold voltage |Vth|", "V", "2"),
                  quantity("k", "Transconductance K", "A/V^2", "20m"),
                  quantity("lambda", "Channel-length modulation", "1/V", "0.01") };
    for (const auto* id : { "njfet", "pjfet" })
        c[id] = { text("value", "Model", "generic_jfet", Storage::Value),
                  quantity("idss", "Idss", "A", "10m"),
                  quantity("pinchoff", "Pinch-off |Vp|", "V", "2") };
    c["switch_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open") };
    c["switch_spdt"] = { choice("state", "Common connects to", { "A", "B" }, "A") };
    c["relay_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open"),
                        quantity("coil_resistance", "Coil resistance", "ohm", "100") };
    c["fuse"] = { quantity("value", "Rating", "A", "1", Storage::Value) };
    c["power_port"] = { text("busName", "Supply net name", "+5V", Storage::BusName, "Ports with the same name are one net; a leading - draws it pointing down") };
    c["net_label"] = { text("busName", "Label", "NET1", Storage::BusName, "Labels with the same name are one net") };
    c["power_bus"] = { text("busName", "Rail name", "+V", Storage::BusName) };
    c["ground_bus"] = { text("busName", "Rail name", "0", Storage::BusName) };
    c["oscilloscope_2ch"] = { choice("time_per_div", "Time / div", { "10u", "20u", "50u", "100u", "200u", "500u", "1m", "2m", "5m", "10m", "20m", "50m" }, "200u"),
                              choice("ch1_volts_per_div", "CH1 volts / div", { "10m", "20m", "50m", "100m", "200m", "500m", "1", "2", "5", "10" }, "500m"),
                              choice("ch2_volts_per_div", "CH2 volts / div", { "10m", "20m", "50m", "100m", "200m", "500m", "1", "2", "5", "10" }, "500m"),
                              quantity("ch1_position", "CH1 position", "div", "0"),
                              quantity("ch2_position", "CH2 position", "div", "0"),
                              choice("trigger_source", "Trigger source", { "CH1", "CH2" }, "CH1"),
                              quantity("trigger_level", "Trigger level", "V", "0"),
                              choice("trigger_slope", "Trigger slope", { "Rising", "Falling" }, "Rising") };
    c["digital_multimeter"] = { choice("value", "Function", { "DC V", "AC V", "DC A", "Ohms" }, "DC V", Storage::Value) };
    c["bode_analyzer"] = { quantity("start_frequency", "Start frequency", "Hz", "10"),
                           quantity("stop_frequency", "Stop frequency", "Hz", "100k"),
                           quantity("points_per_decade", "Points per decade", "", "20") };
    return c;
}

const std::map<juce::String, std::vector<ParamSpec>>& catalog()
{
    static const auto c = buildCatalog();
    return c;
}
}

const std::vector<ParamSpec>& paramsFor(const juce::String& symbolId)
{
    static const std::vector<ParamSpec> none;
    const auto found = catalog().find(symbolId);
    return found != catalog().end() ? found->second : none;
}

const ParamSpec* findParam(const juce::String& symbolId, const juce::String& key)
{
    for (const auto& spec : paramsFor(symbolId))
        if (spec.key == key)
            return &spec;
    return nullptr;
}

juce::String displayName(const juce::String& id)
{
    static const std::map<juce::String, juce::String> names {
        { "resistor", "Resistor" }, { "potentiometer", "Potentiometer" }, { "capacitor", "Capacitor" },
        { "capacitor_polarized", "Polarized Capacitor" }, { "variable_capacitor", "Variable Capacitor" },
        { "inductor", "Inductor" }, { "coupled_inductor", "Coupled Inductors" }, { "transformer", "Transformer" },
        { "diode", "Diode" }, { "zener_diode", "Zener Diode" }, { "schottky_diode", "Schottky Diode" }, { "led", "LED" },
        { "battery", "Battery" }, { "voltage_source", "DC Voltage Source" }, { "current_source", "DC Current Source" },
        { "ac_voltage_source", "AC Voltage Source" }, { "signal_source", "Signal Source" }, { "ac_current_source", "AC Current Source" },
        { "vcvs", "Voltage-Controlled Voltage Source" }, { "vccs", "Voltage-Controlled Current Source" },
        { "ccvs", "Current-Controlled Voltage Source" }, { "cccs", "Current-Controlled Current Source" },
        { "opamp_741", "Op Amp" }, { "npn", "NPN Transistor" }, { "pnp", "PNP Transistor" },
        { "nmos", "N-Channel MOSFET" }, { "pmos", "P-Channel MOSFET" }, { "njfet", "N-Channel JFET" }, { "pjfet", "P-Channel JFET" },
        { "switch_spst", "SPST Switch" }, { "switch_spdt", "SPDT Switch" }, { "relay_spst", "SPST Relay" }, { "fuse", "Fuse" },
        { "connector_2", "2-Pin Connector" }, { "connector_3", "3-Pin Connector" }, { "test_point", "Test Point" },
        { "ground", "Ground" }, { "power_port", "Supply Port" }, { "net_label", "Net Label" },
        { "power_bus", "Power Bus" }, { "ground_bus", "Ground Bus" },
        { "oscilloscope_2ch", "Oscilloscope" }, { "digital_multimeter", "Digital Multimeter" }, { "bode_analyzer", "Frequency Analyzer" },
        { "sub_block", "Sub-diagram Block" }, { "block_port", "Sub-diagram Port" },
        { "logic_not", "Inverter" }, { "logic_and", "AND Gate" }, { "logic_or", "OR Gate" },
        { "logic_nand", "NAND Gate" }, { "logic_nor", "NOR Gate" }, { "logic_xor", "XOR Gate" } };
    const auto found = names.find(id);
    return found != names.end() ? found->second : id;
}

bool validate(const ParamSpec& spec, const juce::String& value, juce::String& error)
{
    switch (spec.kind)
    {
        case Kind::Quantity:
        {
            double parsed = 0.0;
            auto textValue = value.trim();
            // European voltage notation 5V1 = 5.1 V.
            if (spec.unit == "V" && textValue.containsChar('V') && textValue.upToFirstOccurrenceOf("V", false, false).containsOnly("0123456789")
                && textValue.fromFirstOccurrenceOf("V", false, false).containsOnly("0123456789") && textValue.fromFirstOccurrenceOf("V", false, false).isNotEmpty())
                textValue = textValue.replace("V", ".");
            if (!circuit_sim::parseValue(textValue.toStdString(), parsed))
            {
                error = "\"" + value + "\" is not a number" + (spec.unit.isNotEmpty() ? " in " + spec.unit : juce::String())
                      + ". Use forms like 4.7k, 10u, 2.2n, 1meg.";
                return false;
            }
            return true;
        }
        case Kind::Choice:
        case Kind::Toggle:
            if (!spec.options.contains(value))
            {
                error = "Choose one of: " + spec.options.joinIntoString(", ") + ".";
                return false;
            }
            return true;
        case Kind::Fraction:
        {
            const auto v = value.getDoubleValue();
            if (!value.trim().containsOnly("0123456789.") || v < 0.0 || v > 1.0)
            {
                error = "Enter a number from 0 to 1.";
                return false;
            }
            return true;
        }
        case Kind::Text:
            if (value.trim().isEmpty())
            {
                error = spec.label + " cannot be empty.";
                return false;
            }
            return true;
    }
    return true;
}
}

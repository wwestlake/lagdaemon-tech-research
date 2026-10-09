#include "SpiceLibrary.h"
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

ParamSpec when(ParamSpec spec, juce::String condition)
{
    spec.showWhen = std::move(condition);
    return spec;
}

std::vector<ParamSpec> waveformSource(const juce::String& unit)
{
    const auto one = unit == "A" ? juce::String("1m") : juce::String("1");
    return {
        choice("waveform", "Waveform", { "Sine", "Square", "Pulse", "PWL", "Exp" }, "Sine"),
        when(quantity("amplitude", "Amplitude (peak)", unit, one, Storage::Value), "waveform=Sine|Square"),
        when(quantity("frequency", "Frequency", "Hz", "1k", Storage::Frequency), "waveform=Sine|Square"),
        when(quantity("offset", "DC offset", unit, "0"), "waveform=Sine|Square"),
        when(quantity("phase", "Phase", "deg", "0"), "waveform=Sine|Square"),
        when(quantity("duty", "Duty cycle", "", "0.5"), "waveform=Square"),
        when(quantity("offset", "Initial value (V1)", unit, "0"), "waveform=Pulse|Exp"),
        when(quantity("pulsed_value", "Pulsed value (V2)", unit, one), "waveform=Pulse|Exp"),
        when(quantity("delay", "Delay", "s", "0"), "waveform=Pulse|Exp"),
        when(quantity("rise", "Rise time", "s", "1n"), "waveform=Pulse"),
        when(quantity("fall", "Fall time", "s", "1n"), "waveform=Pulse"),
        when(quantity("width", "Pulse width", "s", "500u"), "waveform=Pulse"),
        when(quantity("period", "Period (0 = single pulse)", "s", "1m"), "waveform=Pulse"),
        when(quantity("tau1", "Rise time constant", "s", "100u"), "waveform=Exp"),
        when(quantity("delay2", "Fall starts at", "s", "1m"), "waveform=Exp"),
        when(quantity("tau2", "Fall time constant", "s", "100u"), "waveform=Exp"),
        when(text("pwl", "Points (time value, ...)", "0 0, 1m " + one + ", 2m " + one + ", 3m 0", Storage::Param,
                  "Time-value pairs separated by commas, times increasing; the value holds after the last point."), "waveform=PWL"),
    };
}

std::map<juce::String, std::vector<ParamSpec>> buildCatalog()
{
    std::map<juce::String, std::vector<ParamSpec>> c;
    c["resistor"] = { quantity("value", "Resistance", "ohm", "10k", Storage::Value),
                      quantity("tempco", "Temperature coefficient", "ppm/K", "0", Storage::Param, "Used by temperature analyses (models are at 27 C)") };
    c["potentiometer"] = { quantity("value", "Total resistance", "ohm", "10k", Storage::Value),
                           fraction("position", "Wiper position", "0.5", "0 = pin 1 end, 1 = pin 2 end") };
    c["capacitor"] = { quantity("value", "Capacitance", "F", "1u", Storage::Value),
                       quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; measured from pin 1 to pin 2") };
    c["capacitor_polarized"] = { quantity("value", "Capacitance", "F", "10u", Storage::Value),
                                 quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; measured from + to -") };
    c["variable_capacitor"] = { quantity("value", "Capacitance", "F", "100p", Storage::Value) };
    c["inductor"] = { quantity("value", "Inductance", "H", "10m", Storage::Value),
                      quantity("initial_current", "Initial current", "A", "", Storage::Param, "Blank = unspecified; positive from pin 1 to pin 2") };
    c["coupled_inductor"] = { quantity("value", "Inductance (each)", "H", "10m", Storage::Value),
                              quantity("coupling", "Coupling k", "", "0.99", Storage::Param, "0 to 1") };
    c["transformer"] = { text("value", "Turns ratio (primary:secondary)", "1:1", Storage::Value),
                         quantity("primary_inductance", "Primary inductance", "H", "1"),
                         quantity("coupling", "Coupling k", "", "0.999") };
    c["diode"] = { text("value", "Model", "1N4148", Storage::Value),
                   quantity("saturation_current", "Saturation current Is", "A", "2.52n"),
                   quantity("emission", "Emission coefficient n", "", "1.752"),
                   quantity("cj0", "Junction capacitance Cj0", "F", "4p"),
                   quantity("transit_time", "Transit time TT", "s", "20n") };
    c["zener_diode"] = { quantity("value", "Zener voltage", "V", "5.1", Storage::Value),
                         quantity("saturation_current", "Saturation current Is", "A", "1n"),
                         quantity("emission", "Emission coefficient n", "", "1.5"),
                         quantity("cj0", "Junction capacitance Cj0", "F", "100p"),
                         quantity("transit_time", "Transit time TT", "s", "0") };
    c["schottky_diode"] = { text("value", "Model", "BAT54", Storage::Value),
                            quantity("saturation_current", "Saturation current Is", "A", "100n"),
                            quantity("emission", "Emission coefficient n", "", "1.05"),
                            quantity("cj0", "Junction capacitance Cj0", "F", "10p"),
                            quantity("transit_time", "Transit time TT", "s", "0") };
    c["led"] = { choice("value", "Colour", { "Red", "Green", "Yellow", "Blue", "White" }, "Red", Storage::Value) };
    c["battery"] = { quantity("value", "Voltage", "V", "9", Storage::Value) };
    c["voltage_source"] = { quantity("value", "Voltage", "V", "5", Storage::Value) };
    c["current_source"] = { quantity("value", "Current", "A", "1m", Storage::Value) };
    c["ac_voltage_source"] = waveformSource("V");
    c["signal_source"] = waveformSource("V");
    c["ac_current_source"] = waveformSource("A");
    c["behavioral_voltage_source"] = { text("value", "Voltage expression", "V(CTRL)", Storage::Value, "Xyce expression for source voltage, e.g. 2*V(IN).") };
    c["behavioral_current_source"] = { text("value", "Current expression", "V(CTRL)/1k", Storage::Value, "Xyce expression for source current, e.g. V(IN)/1k.") };
    c["vcvs"] = { quantity("value", "Voltage gain", "V/V", "10", Storage::Value) };
    c["vccs"] = { quantity("value", "Transconductance", "A/V", "1m", Storage::Value) };
    c["ccvs"] = { quantity("value", "Transresistance", "V/A", "1k", Storage::Value) };
    c["cccs"] = { quantity("value", "Current gain", "A/A", "10", Storage::Value) };
    c["opamp_741"] = { text("value", "Model", "uA741", Storage::Value),
                       quantity("gain", "Open-loop gain", "V/V", "200k"),
                       quantity("gbw", "Gain-bandwidth product", "Hz", "1meg", Storage::Param, "Dominant pole at GBW / open-loop gain; 0 = no roll-off"),
                       quantity("headroom", "Output headroom from rails", "V", "1.5") };
    c["opamp_generic"] = { text("value", "Model", "generic_opamp", Storage::Value),
                           quantity("gain", "Open-loop gain", "V/V", "100k"),
                           quantity("headroom", "Output headroom from rails", "V", "0") };
    c["comparator_generic"] = { text("value", "Model", "generic_comparator", Storage::Value),
                                quantity("gain", "Open-loop gain", "V/V", "1meg"),
                                quantity("headroom", "Output headroom from rails", "V", "0") };
    c["comparator_lm311"] = { text("value", "Model", "LM311", Storage::Value),
                              quantity("gain", "Internal preview gain", "V/V", "1meg"),
                              quantity("headroom", "Output headroom from rails", "V", "0.2") };
    c["regulator_fixed_generic"] = { text("value", "Model", "generic_regulator_5v", Storage::Value) };
    c["regulator_adjustable_generic"] = { text("value", "Model", "generic_regulator_adjustable", Storage::Value) };
    c["regulator_lm317"] = { text("value", "Model", "LM317_TRANS", Storage::Value) };
    for (const auto* id : { "npn", "pnp" })
        c[id] = { text("value", "Model", juce::String(id) == "npn" ? "generic_npn" : "generic_pnp", Storage::Value),
                  quantity("beta", "Current gain (beta)", "", "100"),
                  quantity("saturation_current", "Saturation current Is", "A", "10f"),
                  quantity("early_voltage", "Early voltage VAF", "V", "100", Storage::Param, "0 = no Early effect"),
                  quantity("transit_time", "Forward transit time TF", "s", "300p", Storage::Param, "Sets fT with the junction capacitances"),
                  quantity("cje", "B-E junction capacitance", "F", "4.5p"),
                  quantity("cjc", "B-C junction capacitance", "F", "3.5p") };
    for (const auto* id : { "nmos", "pmos" })
        c[id] = { text("value", "Model", juce::String(id) == "nmos" ? "generic_nmos" : "generic_pmos", Storage::Value),
                  quantity("threshold", "Threshold voltage |Vth|", "V", "2"),
                  quantity("k", "Transconductance K", "A/V^2", "20m"),
                  quantity("lambda", "Channel-length modulation", "1/V", "0.01"),
                  quantity("cgs", "Gate-source capacitance", "F", "20p"),
                  quantity("cgd", "Gate-drain capacitance", "F", "5p") };
    for (const auto* id : { "njfet", "pjfet" })
        c[id] = { text("value", "Model", juce::String(id) == "njfet" ? "generic_njfet" : "generic_pjfet", Storage::Value),
                  quantity("idss", "Idss", "A", "10m"),
                  quantity("pinchoff", "Pinch-off |Vp|", "V", "2") };
    c["switch_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open") };
    c["switch_spdt"] = { choice("state", "Common connects to", { "A", "B" }, "A") };
    c["voltage_controlled_switch"] = { quantity("ron", "On resistance", "ohm", "1"),
                                       quantity("roff", "Off resistance", "ohm", "1G"),
                                       quantity("threshold", "Switch threshold", "V", "2.5"),
                                       quantity("hysteresis", "Hysteresis", "V", "0") };
    c["current_controlled_switch"] = { quantity("ron", "On resistance", "ohm", "1"),
                                       quantity("roff", "Off resistance", "ohm", "1G"),
                                       quantity("threshold", "Switch threshold", "A", "1m"),
                                       quantity("hysteresis", "Hysteresis", "A", "0") };
    c["relay_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open"),
                        quantity("coil_resistance", "Coil resistance", "ohm", "100") };
    c["fuse"] = { quantity("value", "Rating", "A", "1", Storage::Value) };
    c["power_port"] = { text("busName", "Supply net name", "+5V", Storage::BusName, "Ports with the same name are one net; a leading - draws it pointing down") };
    c["net_label"] = { text("busName", "Label", "NET1", Storage::BusName, "Labels with the same name are one net"),
                       quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; emits .IC for this net") };
    c["power_bus"] = { text("busName", "Rail name", "+V", Storage::BusName) };
    c["audio_in"] = { choice("source", "Input Type", { "Hardware", "File" }, "Hardware"), text("file", "File Path", "") };
    c["audio_out"] = { choice("sink", "Output Type", { "Hardware", "File" }, "Hardware"), text("file", "File Path", "") };
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
    c["annotation_text"] = { text("value", "Text", "Note", Storage::Value) };
    return c;
}

const std::map<juce::String, std::vector<ParamSpec>>& catalog()
{
    spice_library::initialize(); static std::map<juce::String, std::vector<ParamSpec>> c = buildCatalog();
    return c;
}
}

const std::vector<ParamSpec>& paramsFor(const juce::String& symbolId)
{
    static const std::vector<ParamSpec> none;
    const auto found = catalog().find(symbolId);
    return found != catalog().end() ? found->second : none;
}

bool isShown(const ParamSpec& spec, const std::function<juce::String(const juce::String&)>& valueOf)
{
    if (spec.showWhen.isEmpty())
        return true;
    const auto key = spec.showWhen.upToFirstOccurrenceOf("=", false, false).trim();
    const auto allowed = juce::StringArray::fromTokens(spec.showWhen.fromFirstOccurrenceOf("=", false, false), "|", "");
    return allowed.contains(valueOf(key).trim());
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
        { "opamp_generic", "Generic Op Amp" }, { "opamp_741", "741 Op Amp" },
        { "comparator_generic", "Generic Comparator" }, { "comparator_lm311", "LM311 Comparator" },
        { "regulator_fixed_generic", "Generic Fixed Regulator" }, { "regulator_adjustable_generic", "Generic Adjustable Regulator" },
        { "regulator_lm317", "LM317 Regulator" },
        { "npn", "NPN Transistor" }, { "pnp", "PNP Transistor" },
        { "nmos", "N-Channel MOSFET" }, { "pmos", "P-Channel MOSFET" }, { "njfet", "N-Channel JFET" }, { "pjfet", "P-Channel JFET" },
        { "switch_spst", "SPST Switch" }, { "switch_spdt", "SPDT Switch" },
        { "voltage_controlled_switch", "Voltage-Controlled Switch" }, { "current_controlled_switch", "Current-Controlled Switch" },
        { "relay_spst", "SPST Relay" }, { "fuse", "Fuse" },
        { "connector_2", "2-Pin Connector" }, { "connector_3", "3-Pin Connector" }, { "test_point", "Test Point" },
        { "ground", "Ground" }, { "power_port", "Supply Port" }, { "audio_in", "Audio Input" }, { "audio_out", "Audio Output" }, { "net_label", "Net Label" },
        { "power_bus", "Power Bus" }, { "ground_bus", "Ground Bus" },
        { "oscilloscope_2ch", "Oscilloscope" }, { "digital_multimeter", "Digital Multimeter" }, { "bode_analyzer", "Frequency Analyzer" },
        { "sub_block", "Sub-diagram Block" }, { "block_port", "Sub-diagram Port" },
        { "annotation_text", "Text Note" },
        { "logic_not", "Inverter" }, { "logic_and", "AND Gate" }, { "logic_or", "OR Gate" },
        { "logic_nand", "NAND Gate" }, { "logic_nor", "NOR Gate" }, { "logic_xor", "XOR Gate" } };
    const auto found = names.find(id);
    return found != names.end() ? found->second : id;
}

juce::String simulationFidelity(const juce::String& id)
{
    if (id == "opamp_741" || id == "comparator_lm311" || id == "regulator_lm317")
        return "vendor_model";
    if (id == "opamp_generic" || id == "comparator_generic"
        || id == "regulator_fixed_generic" || id == "regulator_adjustable_generic")
        return "generic_model";
    if (id == "voltage_controlled_switch" || id == "current_controlled_switch")
        return "generic_model";
    if (id == "diode" || id == "zener_diode" || id == "schottky_diode" || id == "led"
        || id == "npn" || id == "pnp" || id == "nmos" || id == "pmos" || id == "njfet" || id == "pjfet")
        return "generic_model";
    if (id == "switch_spst" || id == "switch_spdt" || id.startsWith("logic_") || id == "fuse")
        return "ideal";
    if (id == "relay_spst" || id == "oscilloscope_2ch" || id == "digital_multimeter" || id == "bode_analyzer"
        || id == "audio_out" || id == "sub_block" || id == "block_port" || id == "annotation_text")
        return "unsupported";
    return "primitive";
}

bool validate(const ParamSpec& spec, const juce::String& value, juce::String& error)
{
    switch (spec.kind)
    {
        case Kind::Quantity:
        {
            double parsed = 0.0;
            auto textValue = value.trim();
            if (textValue.isEmpty() && spec.defaultValue.trim().isEmpty())
                return true;
            // European voltage notation 5V1 = 5.1 V.
            if (spec.unit == "V" && textValue.containsChar('V') && textValue.upToFirstOccurrenceOf("V", false, false).containsOnly("0123456789")
                && textValue.fromFirstOccurrenceOf("V", false, false).containsOnly("0123456789") && textValue.fromFirstOccurrenceOf("V", false, false).isNotEmpty())
                textValue = textValue.replace("V", ".");
            auto reject = [&] {
                error = "\"" + value + "\" is not a number" + (spec.unit.isNotEmpty() ? " in " + spec.unit : juce::String())
                      + ". Use forms like 4.7k, 10u, 2.2n, 1meg.";
                return false;
            };
            if (!circuit_sim::parseValue(textValue.toStdString(), parsed))
                return reject();

            // Anything after the number and its prefix must be this value's unit.
            int pos = 0;
            while (pos < textValue.length() && juce::String("0123456789.+-eE").containsChar(textValue[pos]))
            {
                // An 'e' only belongs to the number when digits follow (1e3), not in "meg".
                if ((textValue[pos] == 'e' || textValue[pos] == 'E')
                    && !(pos + 1 < textValue.length() && juce::String("0123456789+-").containsChar(textValue[pos + 1])))
                    break;
                ++pos;
            }
            auto suffix = textValue.substring(pos).trim();
            if (suffix.startsWithIgnoreCase("meg")) suffix = suffix.substring(3);
            else if (suffix.isNotEmpty() && juce::String("TGMkKmunpf").containsChar(suffix[0])) suffix = suffix.substring(1);
            else if (suffix.startsWith(juce::String::fromUTF8("Âµ"))) suffix = suffix.substring(1);
            while (suffix.isNotEmpty() && juce::CharacterFunctions::isDigit(suffix[0])) suffix = suffix.substring(1); // 4k7
            if (suffix.isEmpty())
                return true;
            juce::StringArray accepted { spec.unit };
            if (spec.unit == "ohm") accepted.addArray({ "ohms", "r", juce::String::fromUTF8("Î©") });
            if (spec.unit == "deg") accepted.add(juce::String::fromUTF8("Â°"));
            for (const auto& unit : accepted)
                if (unit.isNotEmpty() && suffix.equalsIgnoreCase(unit))
                    return true;
            return reject();
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
                if (spec.defaultValue.trim().isEmpty())
                    return true;
                error = spec.label + " cannot be empty.";
                return false;
            }
            return true;
    }
    return true;
}
}






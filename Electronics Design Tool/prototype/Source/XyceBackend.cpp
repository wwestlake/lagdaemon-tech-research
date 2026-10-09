#include "XyceBackend.h"

#include "SpiceLibrary.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace xyce_backend
{
namespace
{
using circuit_sim::Element;
using ElementType = Element::Type;

juce::String quotePath(const juce::File& f)
{
    return "\"" + f.getFullPathName().replace("\"", "\\\"") + "\"";
}

juce::String sanitize(juce::String s)
{
    s = s.trim();
    if (s.isEmpty())
        return "N";
    juce::String out;
    for (auto c : s)
    {
        if (c == '+')
            out << "P";
        else if (c == '-')
            out << "N";
        else
            out << (juce::CharacterFunctions::isLetterOrDigit(c) || c == '_' || c == '$' ? juce::String::charToString(c) : "_");
    }
    if (out[0] >= '0' && out[0] <= '9')
        out = "N" + out;
    return out;
}

juce::String value(double v)
{
    return juce::String(v, 12);
}

juce::String elementValue(const Element& e)
{
    return e.valueExpression.empty() ? value(e.value) : "{" + juce::String(e.valueExpression) + "}";
}

juce::String nodeName(const analytics::Netlist& n, circuit_sim::Node node)
{
    if (node == 0)
        return "0";
    for (const auto& net : n.nets)
        if (net.node == node)
            return sanitize(net.name);
    return "N" + juce::String(node);
}

int nodeForOutput(const analytics::Netlist& n, juce::String token, juce::String& error)
{
    token = token.trim();
    if (token.startsWithIgnoreCase("V(") && token.endsWithChar(')'))
        token = token.substring(2, token.length() - 1).trim();
    if (token.equalsIgnoreCase("0") || token.equalsIgnoreCase("GND"))
        return 0;
    for (const auto& net : n.nets)
        if (net.name.equalsIgnoreCase(token))
            return net.node;
    error = "No net named " + token + " for Xyce output selection.";
    return -1;
}

juce::String sourceName(const Element& e, size_t index)
{
    juce::String name = sanitize(e.name);
    const auto prefix = e.type == ElementType::CurrentSource ? "I" : "V";
    if (!name.startsWithIgnoreCase(prefix))
        name = prefix + name;
    if (name == prefix)
        name << (int)index + 1;
    return name;
}

juce::String elementName(const Element& e, size_t index, const juce::String& prefix)
{
    auto name = sanitize(e.name);
    if (!name.startsWithIgnoreCase(prefix))
        name = prefix + name;
    if (name == prefix)
        name << (int)index + 1;
    return name;
}

juce::String elementPrintName(const Element& e, size_t index)
{
    switch (e.type)
    {
        case ElementType::VoltageSource: return sourceName(e, index);
        case ElementType::CurrentSource: return sourceName(e, index);
        case ElementType::Resistor:
        case ElementType::VariableResistor:
        case ElementType::Switch: return elementName(e, index, "R");
        case ElementType::VoltageControlledSwitch: return elementName(e, index, "S");
        case ElementType::CurrentControlledSwitch: return elementName(e, index, "W");
        case ElementType::Capacitor: return elementName(e, index, "C");
        case ElementType::Inductor: return elementName(e, index, "L");
        case ElementType::Diode: return elementName(e, index, "D");
        default: return elementName(e, index, "X");
    }
}

juce::String waveformSyntax(const circuit_sim::Waveform& w)
{
    using K = circuit_sim::Waveform::Kind;
    switch (w.kind)
    {
        case K::Dc:
            return "DC " + value(w.dcValue()) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : 0.0) + " " + value(w.acPhaseDegrees);
        case K::Sine:
            return "DC " + value(w.offset) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : std::max(1.0, std::abs(w.amplitude)))
                + " " + value(w.acPhaseDegrees) + " SIN(" + value(w.offset) + " " + value(w.amplitude) + " " + value(w.frequency)
                + " 0 0 " + value(w.phaseDegrees) + ")";
        case K::Pulse:
            return "DC " + value(w.offset) + " PULSE(" + value(w.offset) + " " + value(w.pulsed) + " " + value(w.delay) + " "
                + value(w.rise) + " " + value(w.fall) + " " + value(w.width) + " " + value(w.period) + ")";
        case K::Pwl:
        {
            juce::String s = "DC " + value(w.dcValue()) + " PWL(";
            for (size_t i = 0; i < w.points.size(); ++i)
            {
                if (i != 0) s << " ";
                s << value(w.points[i].first) << " " << value(w.points[i].second);
            }
            s << ")";
            return s;
        }
        case K::Square:
        {
            const auto period = w.frequency > 0.0 ? 1.0 / w.frequency : 1e-3;
            const auto width = period * std::clamp(w.duty, 0.001, 0.999);
            return "DC " + value(w.offset) + " AC " + value(w.acMagnitude != 0.0 ? w.acMagnitude : std::max(1.0, std::abs(w.amplitude)))
                + " PULSE(" + value(w.offset - w.amplitude) + " " + value(w.offset + w.amplitude) + " 0 1n 1n "
                + value(width) + " " + value(period) + ")";
        }
        case K::Exp:
            return "DC " + value(w.offset) + " EXP(" + value(w.offset) + " " + value(w.pulsed) + " " + value(w.delay) + " "
                + value(w.tau1) + " " + value(w.delay2) + " " + value(w.tau2) + ")";
    }
    return "DC 0";
}

juce::String waveformSyntax(const Element& e)
{
    if (e.wave.kind == circuit_sim::Waveform::Kind::Dc && !e.valueExpression.empty())
        return "DC {" + juce::String(e.valueExpression) + "} AC " + value(e.wave.acMagnitude != 0.0 ? e.wave.acMagnitude : 0.0) + " " + value(e.wave.acPhaseDegrees);
    return waveformSyntax(e.wave);
}

juce::String behavioralExpression(const Element& e)
{
    auto expression = juce::String(e.expression).replaceCharacters("\r\n\t", "   ").trim();
    if (expression.startsWithChar('{') && expression.endsWithChar('}'))
        expression = expression.substring(1, expression.length() - 1).trim();
    return expression;
}

juce::String modelNameFor(const Element& e)
{
    if (!e.modelName.empty())
        return juce::String(e.modelName);
    switch (e.type)
    {
        case ElementType::Diode: return "DGEN";
        case ElementType::Npn: return "NPNGEN";
        case ElementType::Pnp: return "PNPGEN";
        case ElementType::Nmos: return "NMOSGEN";
        case ElementType::Pmos: return "PMOSGEN";
        case ElementType::Njfet: return "NJFGEN";
        case ElementType::Pjfet: return "PJFGEN";
        default: break;
    }
    return {};
}

void appendModelLines(const analytics::Netlist& n, juce::String& netlist)
{
    std::set<juce::String> needed;
    for (const auto& e : n.circuit.elements())
    {
        const auto m = modelNameFor(e);
        if (m.isNotEmpty())
            needed.insert(m);
    }
    if (needed.empty())
        return;
    netlist << "\n* Generated fallback models\n";
    if (needed.count("DGEN")) netlist << ".MODEL DGEN D\n";
    if (needed.count("NPNGEN")) netlist << ".MODEL NPNGEN NPN\n";
    if (needed.count("PNPGEN")) netlist << ".MODEL PNPGEN PNP\n";
    if (needed.count("NMOSGEN")) netlist << ".MODEL NMOSGEN NMOS\n";
    if (needed.count("PMOSGEN")) netlist << ".MODEL PMOSGEN PMOS\n";
    if (needed.count("NJFGEN")) netlist << ".MODEL NJFGEN NJF\n";
    if (needed.count("PJFGEN")) netlist << ".MODEL PJFGEN PJF\n";
}

struct ModelPinBinding
{
    juce::String appPin;
    juce::String modelPin;
    int elementNode = -1;
};

struct ModelBinding
{
    juce::String symbolId;
    juce::String fidelity;
    juce::String requiredModelName;
    juce::String modelKind;
    std::vector<juce::String> lookupNames;
    std::vector<ModelPinBinding> pinsInModelOrder;
    bool modelNameFromElement = false;
};

const std::vector<ModelBinding>& modelBindingRegistry()
{
    static const std::vector<ModelBinding> bindings {
        {
            "opamp_741",
            "vendor_model",
            "UA741",
            "SUBCKT",
            { "UA741", "uA741", "LM741" },
            {
                { "IN+", "1", 0 },
                { "IN-", "2", 1 },
                { "V+",  "3", 3 },
                { "V-",  "4", 4 },
                { "OUT", "5", -1 },
            },
        },
        {
            "opamp_generic",
            "generic_model",
            "generic_opamp",
            "SUBCKT",
            { "generic_opamp" },
            {
                { "IN+", "IN+", 0 },
                { "IN-", "IN-", 1 },
                { "V+",  "V+",  3 },
                { "V-",  "V-",  4 },
                { "OUT", "OUT", 2 },
            },
            true,
        },
        {
            "comparator_generic",
            "generic_model",
            "generic_comparator",
            "SUBCKT",
            { "generic_comparator" },
            {
                { "IN+", "IN+", 0 },
                { "IN-", "IN-", 1 },
                { "V+",  "V+",  3 },
                { "V-",  "V-",  4 },
                { "OUT", "OUT", 2 },
            },
            true,
        },
        {
            "comparator_lm311",
            "vendor_model",
            "LM311",
            "SUBCKT",
            { "LM311" },
            {
                { "IN+",     "IN+",     0 },
                { "IN-",     "IN-",     1 },
                { "V+",      "VCC+",    3 },
                { "V-",      "VCC-",    4 },
                { "STROBE",  "STROB",   5 },
                { "COL_OUT", "COL_OUT", 2 },
                { "EMIT_OUT","EMIT_OUT",6 },
            },
        },
        {
            "regulator_fixed_generic",
            "generic_model",
            "generic_regulator_5v",
            "SUBCKT",
            { "generic_regulator_5v" },
            {
                { "IN",  "IN",  0 },
                { "GND", "GND", 1 },
                { "OUT", "OUT", 2 },
            },
            true,
        },
        {
            "regulator_adjustable_generic",
            "generic_model",
            "generic_regulator_adjustable",
            "SUBCKT",
            { "generic_regulator_adjustable" },
            {
                { "IN",  "IN",  0 },
                { "ADJ", "ADJ", 1 },
                { "OUT", "OUT", 2 },
            },
            true,
        },
        {
            "regulator_lm317",
            "vendor_model",
            "LM317_TRANS",
            "SUBCKT",
            { "LM317_TRANS" },
            {
                { "IN",  "IN",    0 },
                { "ADJ", "ADJ",   1 },
                { "OUT", "OUT_0", 2 },
                { "OUT", "OUT_1", 2 },
            },
        },
        {
            "diode",
            "vendor_model",
            "1N4148",
            "D",
            { "1N4148" },
            { { "A", "A", 0 }, { "K", "K", 1 } },
            true,
        },
        {
            "zener_diode",
            "generic_model",
            "ZENER_5V1",
            "D",
            { "ZENER_5V1" },
            { { "A", "A", 0 }, { "K", "K", 1 } },
            true,
        },
        {
            "schottky_diode",
            "vendor_model",
            "BAT54",
            "D",
            { "BAT54" },
            { { "A", "A", 0 }, { "K", "K", 1 } },
            true,
        },
        {
            "led",
            "generic_model",
            "LED_RED",
            "D",
            { "LED_RED" },
            { { "A", "A", 0 }, { "K", "K", 1 } },
            true,
        },
        {
            "npn",
            "generic_model",
            "generic_npn",
            "NPN",
            { "generic_npn" },
            { { "C", "C", 0 }, { "B", "B", 1 }, { "E", "E", 2 } },
            true,
        },
        {
            "pnp",
            "generic_model",
            "generic_pnp",
            "PNP",
            { "generic_pnp" },
            { { "C", "C", 0 }, { "B", "B", 1 }, { "E", "E", 2 } },
            true,
        },
        {
            "nmos",
            "generic_model",
            "generic_nmos",
            "NMOS|SUBCKT",
            { "generic_nmos" },
            { { "D", "D", 0 }, { "G", "G", 1 }, { "S", "S", 2 }, { "B", "B", 2 } },
            true,
        },
        {
            "pmos",
            "generic_model",
            "generic_pmos",
            "PMOS",
            { "generic_pmos" },
            { { "D", "D", 0 }, { "G", "G", 1 }, { "S", "S", 2 }, { "B", "B", 2 } },
            true,
        },
        {
            "njfet",
            "generic_model",
            "generic_njfet",
            "NJF",
            { "generic_njfet" },
            { { "D", "D", 0 }, { "G", "G", 1 }, { "S", "S", 2 } },
            true,
        },
        {
            "pjfet",
            "generic_model",
            "generic_pjfet",
            "PJF",
            { "generic_pjfet" },
            { { "D", "D", 0 }, { "G", "G", 1 }, { "S", "S", 2 } },
            true,
        },
        {
            "voltage_controlled_switch",
            "generic_model",
            "generated_sw",
            "SW",
            { "generated_sw" },
            { { "1", "1", 0 }, { "2", "2", 1 }, { "CP+", "CP+", 2 }, { "CP-", "CP-", 3 } },
            true,
        },
        {
            "current_controlled_switch",
            "generic_model",
            "generated_csw",
            "CSW",
            { "generated_csw" },
            { { "1", "1", 0 }, { "2", "2", 1 }, { "SENSE", "SENSE", -1 } },
            true,
        },
    };
    return bindings;
}

const ModelBinding* modelBindingFor(const juce::String& symbolId)
{
    for (const auto& binding : modelBindingRegistry())
        if (binding.symbolId == symbolId)
            return &binding;
    return nullptr;
}

const ModelBinding* modelBindingForElement(const analytics::Netlist& n, size_t elementIndex)
{
    for (const auto& part : n.parts)
        if (part.element == (int)elementIndex)
            if (const auto* binding = modelBindingFor(part.symbolId))
                return binding;
    return nullptr;
}

bool modelKindMatches(const juce::String& allowedKinds, const juce::String& actualKind)
{
    for (const auto& kind : juce::StringArray::fromTokens(allowedKinds, "|", ""))
        if (actualKind.equalsIgnoreCase(kind.trim()))
            return true;
    return false;
}

bool isVerifiedNmosSubckt(const spice_library::ModelDef& model)
{
    return model.name.equalsIgnoreCase("Si4778DY") && model.kind.equalsIgnoreCase("SUBCKT");
}

const spice_library::ModelDef* resolveBoundModel(const ModelBinding& binding, juce::String& error)
{
    static const bool initialized = [] {
        spice_library::initialize();
        return true;
    }();
    juce::ignoreUnused(initialized);

    const spice_library::ModelDef* wrongKind = nullptr;
    for (const auto& name : binding.lookupNames)
        if (const auto* def = spice_library::findModel(name); def != nullptr)
        {
            if (modelKindMatches(binding.modelKind, def->kind))
                return def;
            if (wrongKind == nullptr)
                wrongKind = def;
        }

    if (wrongKind != nullptr)
        error = binding.symbolId + " requires " + binding.requiredModelName + " as " + binding.modelKind
              + ", but found " + wrongKind->name + " as " + wrongKind->kind + ".";
    else if (binding.symbolId == "opamp_741")
        error = "UA741 model is missing from the SPICE model library. Expected a SUBCKT named UA741.";
    else
        error = binding.requiredModelName + " model is missing from the SPICE model library. Expected a "
              + binding.modelKind + " named " + binding.requiredModelName + ".";

    return nullptr;
}

const spice_library::ModelDef* resolveBoundModel(const ModelBinding& binding, const Element& e, juce::String& error)
{
    if (!binding.modelNameFromElement)
        return resolveBoundModel(binding, error);
    auto resolved = binding;
    const auto selected = modelNameFor(e).trim();
    if (selected.isNotEmpty())
    {
        resolved.requiredModelName = selected;
        resolved.lookupNames = { selected };
    }
    return resolveBoundModel(resolved, error);
}

bool isPrimaryUa741(const analytics::Netlist& n, size_t elementIndex)
{
    const auto* binding = modelBindingForElement(n, elementIndex);
    return binding != nullptr && binding->symbolId == "opamp_741";
}

bool hasPrimaryUa741Named(const analytics::Netlist& n, const juce::String& name)
{
    for (const auto& part : n.parts)
        if (part.symbolId == "opamp_741" && part.element >= 0 && part.element < (int)n.circuit.elements().size())
            if (juce::String(n.circuit.elements()[(size_t)part.element].name) == name)
                return true;
    return false;
}

bool shouldSkipUa741Internal(const analytics::Netlist& n, const Element& e, size_t elementIndex)
{
    if (isPrimaryUa741(n, elementIndex))
        return false;

    const auto name = juce::String(e.name);
    for (const auto& suffix : { ".out", ".rp", ".cp" })
        if (name.endsWithIgnoreCase(suffix))
            return hasPrimaryUa741Named(n, name.dropLastCharacters(juce::String(suffix).length()));

    return false;
}

circuit_sim::Node ua741OutputNode(const analytics::Netlist& n, const Element& e)
{
    const auto outputStageName = juce::String(e.name) + ".out";
    for (const auto& candidate : n.circuit.elements())
        if (candidate.type == ElementType::OpAmp && juce::String(candidate.name) == outputStageName && candidate.nodes.size() >= 3)
            return candidate.nodes[2];
    return e.nodes[2];
}

circuit_sim::Node nodeForBoundPin(const analytics::Netlist& n, const Element& e, const ModelPinBinding& pin)
{
    if (pin.appPin == "OUT")
        return ua741OutputNode(n, e);
    return e.nodes[(size_t)pin.elementNode];
}

bool appendElement(const analytics::Netlist& n, juce::String& netlist, std::map<int, juce::String>& voltageSourceNames,
                   std::set<juce::String>& subcircuits, const Element& e, size_t index, juce::String& error)
{
    if (shouldSkipUa741Internal(n, e, index))
        return true;

    auto nd = [&](size_t i) { return nodeName(n, e.nodes[i]); };
    switch (e.type)
    {
        case ElementType::Resistor:
        case ElementType::VariableResistor:
        case ElementType::Switch:
            netlist << elementName(e, index, "R") << " " << nd(0) << " " << nd(1) << " " << elementValue(e) << "\n";
            return true;
        case ElementType::VoltageControlledSwitch:
        {
            const auto model = elementName(e, index, "S") + "_MODEL";
            subcircuits.insert(".MODEL " + model + " SW(Ron=" + value(std::max(e.value, 1e-9))
                               + " Roff=" + value(std::max(e.offResistance, 1e-9))
                               + " Vt=" + value(e.threshold)
                               + " Vh=" + value(e.hysteresis) + ")");
            netlist << elementName(e, index, "S") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(3)
                    << " " << model << "\n";
            return true;
        }
        case ElementType::CurrentControlledSwitch:
        {
            const auto found = voltageSourceNames.find(e.control);
            if (found == voltageSourceNames.end())
            {
                error = juce::String(e.name) + " controls a source that has not been lowered to Xyce.";
                return false;
            }
            const auto model = elementName(e, index, "W") + "_MODEL";
            subcircuits.insert(".MODEL " + model + " CSW(Ron=" + value(std::max(e.value, 1e-9))
                               + " Roff=" + value(std::max(e.offResistance, 1e-9))
                               + " It=" + value(e.threshold)
                               + " Ih=" + value(e.hysteresis) + ")");
            netlist << elementName(e, index, "W") << " " << nd(0) << " " << nd(1) << " " << found->second
                    << " " << model << "\n";
            return true;
        }
        case ElementType::Capacitor:
            netlist << elementName(e, index, "C") << " " << nd(0) << " " << nd(1) << " " << elementValue(e);
            if (e.hasInitialCondition)
                netlist << " IC=" << value(e.initialCondition);
            netlist << "\n";
            return true;
        case ElementType::Inductor:
            netlist << elementName(e, index, "L") << " " << nd(0) << " " << nd(1) << " " << elementValue(e);
            if (e.hasInitialCondition)
                netlist << " IC=" << value(e.initialCondition);
            netlist << "\n";
            return true;
        case ElementType::VoltageSource:
        {
            const auto name = sourceName(e, index);
            voltageSourceNames[(int)index] = name;
            netlist << name << " " << nd(0) << " " << nd(1) << " " << waveformSyntax(e) << "\n";
            return true;
        }
        case ElementType::CurrentSource:
            netlist << sourceName(e, index) << " " << nd(0) << " " << nd(1) << " " << waveformSyntax(e) << "\n";
            return true;
        case ElementType::BehavioralVoltageSource:
        case ElementType::BehavioralCurrentSource:
        {
            const auto expression = behavioralExpression(e);
            if (expression.isEmpty())
            {
                error = juce::String(e.name) + " has an empty behavioral source expression.";
                return false;
            }
            netlist << elementName(e, index, "B") << " " << nd(0) << " " << nd(1)
                    << " " << (e.type == ElementType::BehavioralVoltageSource ? "V" : "I")
                    << "={" << expression << "}\n";
            return true;
        }
        case ElementType::Vcvs:
            netlist << elementName(e, index, "E") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(3) << " " << value(e.value) << "\n";
            return true;
        case ElementType::Vccs:
            netlist << elementName(e, index, "G") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(3) << " " << value(e.value) << "\n";
            return true;
        case ElementType::Ccvs:
        case ElementType::Cccs:
        {
            const auto found = voltageSourceNames.find(e.control);
            if (found == voltageSourceNames.end())
            {
                error = juce::String(e.name) + " controls a source that has not been lowered to Xyce.";
                return false;
            }
            netlist << elementName(e, index, e.type == ElementType::Ccvs ? "H" : "F") << " " << nd(0) << " " << nd(1)
                    << " " << found->second << " " << value(e.value) << "\n";
            return true;
        }
        case ElementType::Diode:
        {
            const auto* binding = modelBindingForElement(n, index);
            if (binding != nullptr)
            {
                const auto* model = resolveBoundModel(*binding, e, error);
                if (model == nullptr)
                    return false;
                subcircuits.insert(model->rawText);
                netlist << elementName(e, index, "D");
                for (const auto& pin : binding->pinsInModelOrder)
                {
                    if (pin.elementNode < 0 || pin.elementNode >= (int)e.nodes.size())
                    {
                        error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                        return false;
                    }
                    netlist << " " << nodeName(n, e.nodes[(size_t)pin.elementNode]);
                }
                netlist << " " << model->name << "\n";
                return true;
            }
            netlist << elementName(e, index, "D") << " " << nd(0) << " " << nd(1) << " DGEN\n";
            return true;
        }
        case ElementType::Npn:
        case ElementType::Pnp:
        {
            const auto* binding = modelBindingForElement(n, index);
            if (binding != nullptr)
            {
                const auto* model = resolveBoundModel(*binding, e, error);
                if (model == nullptr)
                    return false;
                if ((e.type == ElementType::Npn && !model->kind.equalsIgnoreCase("NPN"))
                    || (e.type == ElementType::Pnp && !model->kind.equalsIgnoreCase("PNP")))
                {
                    error = juce::String(e.name) + " selected " + model->name + " as " + model->kind
                          + ", incompatible with " + (e.type == ElementType::Npn ? "NPN" : "PNP") + " symbol.";
                    return false;
                }
                subcircuits.insert(model->rawText);
                netlist << elementName(e, index, "Q");
                for (const auto& pin : binding->pinsInModelOrder)
                {
                    if (pin.elementNode < 0 || pin.elementNode >= (int)e.nodes.size())
                    {
                        error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                        return false;
                    }
                    netlist << " " << nodeName(n, e.nodes[(size_t)pin.elementNode]);
                }
                netlist << " " << model->name << "\n";
                return true;
            }
            netlist << elementName(e, index, "Q") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        }
        case ElementType::Nmos:
        case ElementType::Pmos:
        {
            const auto* binding = modelBindingForElement(n, index);
            if (binding != nullptr)
            {
                const auto* model = resolveBoundModel(*binding, e, error);
                if (model == nullptr)
                    return false;
                if (e.type == ElementType::Nmos && isVerifiedNmosSubckt(*model))
                {
                    subcircuits.insert(model->rawText);
                    netlist << elementName(e, index, "X");
                    for (const auto& pinName : { "D", "G", "S" })
                    {
                        const auto found = std::find_if(binding->pinsInModelOrder.begin(), binding->pinsInModelOrder.end(),
                            [pinName](const ModelPinBinding& pin) { return pin.appPin == pinName; });
                        if (found == binding->pinsInModelOrder.end() || found->elementNode < 0 || found->elementNode >= (int)e.nodes.size())
                        {
                            error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                            return false;
                        }
                        netlist << " " << nodeName(n, e.nodes[(size_t)found->elementNode]);
                    }
                    netlist << " " << model->name << "\n";
                    return true;
                }
                if ((e.type == ElementType::Nmos && !model->kind.equalsIgnoreCase("NMOS"))
                    || (e.type == ElementType::Pmos && !model->kind.equalsIgnoreCase("PMOS")))
                {
                    error = juce::String(e.name) + " selected " + model->name + " as " + model->kind
                          + ", incompatible with " + (e.type == ElementType::Nmos ? "NMOS" : "PMOS") + " symbol.";
                    return false;
                }
                subcircuits.insert(model->rawText);
                netlist << elementName(e, index, "M");
                for (const auto& pin : binding->pinsInModelOrder)
                {
                    if (pin.elementNode < 0 || pin.elementNode >= (int)e.nodes.size())
                    {
                        error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                        return false;
                    }
                    netlist << " " << nodeName(n, e.nodes[(size_t)pin.elementNode]);
                }
                netlist << " " << model->name << "\n";
                return true;
            }
            netlist << elementName(e, index, "M") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        }
        case ElementType::Njfet:
        case ElementType::Pjfet:
        {
            const auto* binding = modelBindingForElement(n, index);
            if (binding != nullptr)
            {
                const auto* model = resolveBoundModel(*binding, e, error);
                if (model == nullptr)
                    return false;
                if ((e.type == ElementType::Njfet && !model->kind.equalsIgnoreCase("NJF"))
                    || (e.type == ElementType::Pjfet && !model->kind.equalsIgnoreCase("PJF")))
                {
                    error = juce::String(e.name) + " selected " + model->name + " as " + model->kind
                          + ", incompatible with " + (e.type == ElementType::Njfet ? "NJF" : "PJF") + " symbol.";
                    return false;
                }
                subcircuits.insert(model->rawText);
                netlist << elementName(e, index, "J");
                for (const auto& pin : binding->pinsInModelOrder)
                {
                    if (pin.elementNode < 0 || pin.elementNode >= (int)e.nodes.size())
                    {
                        error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                        return false;
                    }
                    netlist << " " << nodeName(n, e.nodes[(size_t)pin.elementNode]);
                }
                netlist << " " << model->name << "\n";
                return true;
            }
            netlist << elementName(e, index, "J") << " " << nd(0) << " " << nd(1) << " " << nd(2) << " " << modelNameFor(e) << "\n";
            return true;
        }
        case ElementType::Coupling:
        {
            const auto a = elementName(n.circuit.elements()[(size_t)e.control], (size_t)e.control, "L");
            const auto b = elementName(n.circuit.elements()[(size_t)e.control2], (size_t)e.control2, "L");
            netlist << elementName(e, index, "K") << " " << a << " " << b << " " << value(e.value) << "\n";
            return true;
        }
        case ElementType::OpAmp:
        {
            const auto* binding = modelBindingForElement(n, index);
            if (binding == nullptr)
            {
                error = juce::String(e.name) + " is an op amp with no Xyce model binding. Use opamp_741 or add an explicit model mapping.";
                return false;
            }

            const auto* model = resolveBoundModel(*binding, e, error);
            if (model == nullptr)
                return false;

            auto modelText = spice_library::resolveModelTextWithDependencies(model->name);
            subcircuits.insert(modelText.isNotEmpty() ? modelText : model->rawText);
            netlist << elementName(e, index, "X");
            for (const auto& pin : binding->pinsInModelOrder)
            {
                if (pin.elementNode >= 0 && pin.elementNode >= (int)e.nodes.size())
                {
                    error = binding->symbolId + " model pin mapping is incompatible with " + juce::String(e.name) + ".";
                    return false;
                }
                netlist << " " << nodeName(n, nodeForBoundPin(n, e, pin));
            }
            netlist << " " << model->name << "\n";
            return true;
        }
    }
    error = juce::String(e.name) + " is not supported by the Xyce backend.";
    return false;
}

juce::String printList(const analytics::Netlist& n, bool currents)
{
    juce::String s;
    for (const auto& net : n.nets)
        if (net.node != 0)
            s << " V(" << nodeName(n, net.node) << ")";
    if (currents)
        for (const auto& e : n.circuit.elements())
            if (e.type == ElementType::VoltageSource)
                s << " I(" << sourceName(e, 0).dropLastCharacters(sourceName(e, 0).length()) << ")"; // not used
    return s;
}

juce::String selectedPrints(const analytics::Netlist& n, const analytics::Settings& s, bool ac, juce::String& error)
{
    juce::StringArray tokens;
    const auto found = s.find("outputs");
    if (found != s.end())
        tokens = juce::StringArray::fromTokens(found->second, ",;", "");
    tokens.trim();
    tokens.removeEmptyStrings();
    if (tokens.size() == 1 && tokens[0].containsChar(' '))
    {
        tokens = juce::StringArray::fromTokens(tokens[0], " ", "");
        tokens.removeEmptyStrings();
    }

    juce::String out;
    if (tokens.isEmpty())
    {
        int count = 0;
        for (const auto& net : n.nets)
            if (net.node != 0 && count++ < 16)
                out << (ac ? " VM(" : " V(") << nodeName(n, net.node) << ")" << (ac ? " VP(" + nodeName(n, net.node) + ")" : "");
        return out.isEmpty() ? juce::String(ac ? " VM(0) VP(0)" : " V(0)") : out;
    }

    for (const auto& token : tokens)
    {
        if (token.startsWithIgnoreCase("I(") && token.endsWithChar(')'))
        {
            if (ac)
            {
                error = "Xyce AC output selection currently supports voltage outputs only.";
                return {};
            }
            const auto part = token.substring(2, token.length() - 1).trim();
            bool foundPart = false;
            for (const auto& p : n.parts)
                if (p.refdes.equalsIgnoreCase(part) && p.element >= 0 && p.element < (int)n.circuit.elements().size())
                {
                    out << " I(" << elementPrintName(n.circuit.elements()[(size_t)p.element], (size_t)p.element) << ")";
                    foundPart = true;
                    break;
                }
            if (!foundPart)
            {
                error = "No part " + part + " for Xyce output selection.";
                return {};
            }
            continue;
        }

        const auto node = nodeForOutput(n, token, error);
        if (node < 0)
            return {};
        out << (ac ? " VM(" : " V(") << nodeName(n, (circuit_sim::Node)node) << ")";
        if (ac)
            out << " VP(" << nodeName(n, (circuit_sim::Node)node) << ")";
    }
    return out;
}

juce::String acColumnSignal(const juce::String& column, const juce::String& function)
{
    const auto prefix = function + "(";
    if (!column.startsWithIgnoreCase(prefix) || !column.endsWithChar(')'))
        return {};
    return column.substring(prefix.length(), column.length() - 1);
}

juce::String acTraceLabel(const juce::String& signal)
{
    return "V(" + signal + ")";
}

bool hasDeviceInitialConditions(const circuit_sim::Circuit& circuit)
{
    for (const auto& e : circuit.elements())
        if ((e.type == ElementType::Capacitor || e.type == ElementType::Inductor) && e.hasInitialCondition)
            return true;
    return false;
}

juce::String netlistFor(analytics::Analysis analysis, const analytics::Settings& s, const analytics::Netlist& n, juce::String& error)
{
    if (n.error.isNotEmpty())
    {
        error = n.error;
        return {};
    }
    juce::String out;
    out << "* Djehuti Workbench Xyce backend netlist\n";
    out << "* Generated from canonical Workbench Analytics netlist\n\n";
    if (!n.parameters.empty())
    {
        for (const auto& [name, expr] : n.parameters)
            out << ".PARAM " << name << "={" << expr << "}\n";
        out << "\n";
    }
    std::map<int, juce::String> voltageSourceNames;
    std::set<juce::String> subcircuits;
    juce::String body;
    for (size_t i = 0; i < n.circuit.elements().size(); ++i)
        if (!appendElement(n, body, voltageSourceNames, subcircuits, n.circuit.elements()[i], i, error))
            return {};
    if (!subcircuits.empty())
    {
        out << "\n* SPICE model bindings\n";
        for (const auto& subcircuit : subcircuits)
            out << subcircuit.trim() << "\n";
    }
    appendModelLines(n, out);
    out << "\n" << body;

    if (analysis == analytics::Analysis::OperatingPoint)
    {
        out << "\n.OP\n.PRINT DC" << selectedPrints(n, s, false, error);
    }
    else if (analysis == analytics::Analysis::Transient)
    {
        const auto stop = s.count("stop") != 0 && !s.at("stop").equalsIgnoreCase("auto") ? s.at("stop") : "10m";
        const auto step = s.count("step") != 0 && !s.at("step").equalsIgnoreCase("auto") ? s.at("step") : "10u";
        const auto prints = selectedPrints(n, s, false, error);
        if (prints.isEmpty()) return {};
        if (!n.circuit.nodeInitialVoltages().empty())
        {
            out << "\n.IC";
            for (const auto& [node, volts] : n.circuit.nodeInitialVoltages())
                out << " V(" << nodeName(n, node) << ")=" << value(volts);
            out << "\n";
        }
        out << "\n.TRAN " << step << " " << stop << (hasDeviceInitialConditions(n.circuit) ? " UIC" : "") << "\n.PRINT TRAN" << prints;
    }
    else if (analysis == analytics::Analysis::Ac)
    {
        const auto start = s.count("start") != 0 ? s.at("start") : "10";
        const auto stop = s.count("stop") != 0 ? s.at("stop") : "100k";
        const auto points = s.count("points") != 0 ? s.at("points") : "50";
        const auto prints = selectedPrints(n, s, true, error);
        if (prints.isEmpty()) return {};
        out << "\n.AC DEC " << points << " " << start << " " << stop << "\n.PRINT AC" << prints;
    }
    else
    {
        error = "The Xyce backend currently supports Operating Point, Transient and AC/Bode. Select Internal Solver for "
            + analytics::infoFor(analysis).title + " until its Xyce adapter is implemented.";
        return {};
    }

    out << "\n.END\n";
    return out;
}

juce::Array<juce::StringArray> readPrn(const juce::File& file)
{
    auto splitWhitespace = [](const juce::String& text) {
        juce::StringArray tokens;
        juce::String current;
        for (auto c : text)
        {
            if (juce::CharacterFunctions::isWhitespace(c))
            {
                if (current.isNotEmpty())
                {
                    tokens.add(current);
                    current.clear();
                }
            }
            else
            {
                current << juce::String::charToString(c);
            }
        }
        if (current.isNotEmpty())
            tokens.add(current);
        return tokens;
    };

    juce::StringArray lines;
    file.readLines(lines);
    juce::Array<juce::StringArray> rows;
    for (const auto& line : lines)
    {
        const auto t = line.trim();
        if (t.isEmpty() || t.startsWithIgnoreCase("End of"))
            continue;
        rows.add(splitWhitespace(t));
    }
    return rows;
}

bool parseNumber(const juce::String& s, double& valueOut)
{
    valueOut = s.getDoubleValue();
    return s.containsAnyOf("0123456789");
}

analytics::Result resultFromPrn(analytics::Analysis analysis, const analytics::Settings& settings, const juce::File& prn,
                                const analytics::Netlist& n, double seconds)
{
    analytics::Result r;
    r.ok = true;
    r.analysis = analysis;
    r.title = analytics::infoFor(analysis).title + " - Xyce";
    r.settings = settings;
    r.when = juce::Time::getCurrentTime();
    r.seconds = seconds;
    const auto rows = readPrn(prn);
    if (rows.size() < 2)
    {
        r.ok = false;
        r.error = "Xyce produced no parseable .prn data: " + prn.getFullPathName();
        return r;
    }
    const auto header = rows[0];
    if (analysis == analytics::Analysis::OperatingPoint)
    {
        analytics::Table table;
        table.title = "Xyce operating point";
        table.columns = { "Signal", "Value" };
        for (int c = 1; c < header.size() && c < rows[1].size(); ++c)
            table.rows.push_back({ header[c], analytics::formatNumber(rows[1][c].getDoubleValue(), header[c].startsWithIgnoreCase("V(") ? "V" : "A", 7) });
        r.tables.push_back(std::move(table));
        r.summary = "Xyce operating point completed; " + juce::String(header.size() - 1) + " signal(s).";
        return r;
    }

    analytics::Plot plot;
    plot.kind = analytics::Plot::Kind::Lines;
    plot.title = analysis == analytics::Analysis::Ac ? "Xyce AC response" : "Xyce transient";
    plot.xLabel = analysis == analytics::Analysis::Ac ? "Frequency" : "Time";
    plot.xUnit = analysis == analytics::Analysis::Ac ? "Hz" : "s";
    plot.yLabel = analysis == analytics::Analysis::Ac ? "Magnitude / phase" : "Voltage";
    plot.yUnit = analysis == analytics::Analysis::Ac ? "" : "V";
    plot.logX = analysis == analytics::Analysis::Ac;
    const bool hasDomainColumn = header.size() > 1
        && (header[1].equalsIgnoreCase("FREQ") || header[1].equalsIgnoreCase("TIME"));
    const int xColumn = hasDomainColumn ? 1 : 0;
    const int firstTraceColumn = hasDomainColumn ? 2 : 1;
    if (analysis == analytics::Analysis::Ac)
    {
        struct AcColumns
        {
            juce::String signal;
            int magnitude = -1;
            int phase = -1;
        };
        std::vector<AcColumns> columns;
        auto indexFor = [&](const juce::String& signal) -> int {
            for (int i = 0; i < (int)columns.size(); ++i)
                if (columns[(size_t)i].signal.equalsIgnoreCase(signal))
                    return i;
            columns.push_back({ signal, -1, -1 });
            return (int)columns.size() - 1;
        };
        for (int c = firstTraceColumn; c < header.size(); ++c)
        {
            const auto magSignal = acColumnSignal(header[c], "VM");
            if (magSignal.isNotEmpty())
            {
                columns[(size_t)indexFor(magSignal)].magnitude = c;
                continue;
            }
            const auto phaseSignal = acColumnSignal(header[c], "VP");
            if (phaseSignal.isNotEmpty())
                columns[(size_t)indexFor(phaseSignal)].phase = c;
        }

        analytics::Plot mag, phase;
        mag.kind = phase.kind = analytics::Plot::Kind::Lines;
        mag.title = "Xyce AC magnitude";
        mag.xLabel = phase.xLabel = "Frequency";
        mag.xUnit = phase.xUnit = "Hz";
        mag.yLabel = "Magnitude";
        mag.yUnit = "dB";
        mag.logX = true;
        phase.title = "Xyce AC phase";
        phase.yLabel = "Phase";
        phase.yUnit = "deg";
        phase.logX = true;

        for (const auto& ac : columns)
        {
            if (ac.magnitude >= 0)
            {
                analytics::Trace t;
                t.name = acTraceLabel(ac.signal);
                t.unit = "dB";
                for (int rix = 1; rix < rows.size(); ++rix)
                {
                    if (rows[rix].size() <= ac.magnitude)
                        continue;
                    t.x.push_back(rows[rix][xColumn].getDoubleValue());
                    const auto magnitude = rows[rix][ac.magnitude].getDoubleValue();
                    t.y.push_back(20.0 * std::log10(std::max(1e-30, std::abs(magnitude))));
                }
                t.partnerTrace = (int)phase.traces.size();
                mag.traces.push_back(std::move(t));
            }
            if (ac.phase >= 0)
            {
                analytics::Trace t;
                t.name = acTraceLabel(ac.signal);
                t.unit = "deg";
                for (int rix = 1; rix < rows.size(); ++rix)
                {
                    if (rows[rix].size() <= ac.phase)
                        continue;
                    t.x.push_back(rows[rix][xColumn].getDoubleValue());
                    t.y.push_back(rows[rix][ac.phase].getDoubleValue());
                }
                t.partnerTrace = (int)mag.traces.size() - 1;
                phase.traces.push_back(std::move(t));
            }
        }

        if (!mag.traces.empty() && !phase.traces.empty())
        {
            const auto magIndex = (int)r.plots.size();
            const auto phaseIndex = magIndex + 1;
            for (auto& t : mag.traces)
                t.partnerPlot = phaseIndex;
            for (auto& t : phase.traces)
                t.partnerPlot = magIndex;
        }
        std::vector<analytics::Trace> stats = mag.traces;
        stats.insert(stats.end(), phase.traces.begin(), phase.traces.end());
        if (!stats.empty())
            analytics::addTraceStatsTable(r, stats, false);
        if (!mag.traces.empty())
            r.plots.push_back(std::move(mag));
        if (!phase.traces.empty())
            r.plots.push_back(std::move(phase));
        r.summary = "Xyce " + analytics::infoFor(analysis).title + " completed; " + juce::String(rows.size() - 1) + " sample(s).";
        juce::ignoreUnused(n);
        return r;
    }
    for (int c = firstTraceColumn; c < header.size(); ++c)
    {
        analytics::Trace trace;
        trace.name = header[c];
        trace.unit = header[c].startsWithIgnoreCase("V(") || header[c].startsWithIgnoreCase("VM(") ? "V" : header[c].startsWithIgnoreCase("VP(") ? "deg" : "";
        for (int rix = 1; rix < rows.size(); ++rix)
        {
            if (rows[rix].size() <= c)
                continue;
            trace.x.push_back(rows[rix][xColumn].getDoubleValue());
            trace.y.push_back(rows[rix][c].getDoubleValue());
        }
        plot.traces.push_back(std::move(trace));
    }
    if (!plot.traces.empty())
        analytics::addTraceStatsTable(r, plot.traces, analysis == analytics::Analysis::Transient);
    r.plots.push_back(std::move(plot));
    r.summary = "Xyce " + analytics::infoFor(analysis).title + " completed; " + juce::String(rows.size() - 1) + " sample(s).";
    juce::ignoreUnused(n);
    return r;
}

juce::String processOutput(juce::ChildProcess& process)
{
    juce::MemoryOutputStream output;
    while (process.isRunning())
    {
        output << process.readAllProcessOutput();
        juce::Thread::sleep(20);
    }
    output << process.readAllProcessOutput();
    return output.toString();
}
}

juce::String engineName(Engine engine)
{
    return engine == Engine::Xyce ? "Xyce" : "Internal Solver";
}

juce::File configuredExecutable()
{
    const auto config = juce::File::getCurrentWorkingDirectory().getChildFile("config").getChildFile("xyce.local.json");
    if (config.existsAsFile())
    {
        const auto parsed = juce::JSON::parse(config);
        const auto path = parsed.getProperty("xyceExe", {}).toString();
        if (path.isNotEmpty())
            return juce::File(path);
    }
    for (const auto& p : { "C:\\Program Files\\XyceNF_7.10\\bin\\Xyce.exe",
                           "C:\\Program Files\\Xyce 7.10 NORAD\\bin\\Xyce.exe",
                           "C:\\Program Files\\Xyce\\bin\\Xyce.exe" })
        if (juce::File(p).existsAsFile())
            return juce::File(p);
    return {};
}

bool isConfigured(juce::String& detail)
{
    const auto exe = configuredExecutable();
    if (!exe.existsAsFile())
    {
        detail = "Xyce executable is not configured. Run tools/xyce/setup_xyce.ps1.";
        return false;
    }
    detail = exe.getFullPathName();
    return true;
}

analytics::Result run(analytics::Analysis analysis, const analytics::Settings& settings, const analytics::Netlist& netlist,
                      const juce::File& outputRoot)
{
    analytics::Result failed;
    failed.analysis = analysis;
    failed.title = analytics::infoFor(analysis).title + " - Xyce";
    failed.settings = settings;
    failed.when = juce::Time::getCurrentTime();

    if (netlist.circuit.elements().empty())
    {
        failed.error = "The diagram has nothing to simulate.";
        return failed;
    }

    juce::String detail;
    if (!isConfigured(detail))
    {
        failed.error = detail;
        return failed;
    }

    juce::String netlistError;
    const auto netlistText = netlistFor(analysis, settings, netlist, netlistError);
    if (netlistText.isEmpty())
    {
        failed.error = netlistError;
        return failed;
    }

    auto root = outputRoot.exists() ? outputRoot : juce::File::getCurrentWorkingDirectory().getChildFile("sim").getChildFile("xyce").getChildFile("runs");
    auto runDir = root.getChildFile("xyce_backend").getChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"));
    runDir.createDirectory();
    const auto cir = runDir.getChildFile("generated.cir");
    if (!cir.replaceWithText(netlistText))
    {
        failed.error = "Could not write Xyce netlist: " + cir.getFullPathName();
        return failed;
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();
    juce::ChildProcess process;
    const auto command = quotePath(configuredExecutable()) + " " + quotePath(cir);
    if (!process.start(command))
    {
        failed.error = "Could not start Xyce process: " + command;
        return failed;
    }
    const auto stdoutText = processOutput(process);
    runDir.getChildFile("xyce_stdout.txt").replaceWithText(stdoutText);
    const auto exitCode = process.getExitCode();
    if (exitCode != 0)
    {
        failed.error = "Xyce failed with exit code " + juce::String(exitCode) + ". See " + runDir.getChildFile("xyce_stdout.txt").getFullPathName();
        return failed;
    }

    auto prn = cir.getSiblingFile(cir.getFileName() + ".prn");
    if (!prn.existsAsFile())
    {
        const auto prns = runDir.findChildFiles(juce::File::findFiles, false, cir.getFileName() + "*.prn");
        if (!prns.isEmpty())
            prn = prns[0];
    }
    if (!prn.existsAsFile())
    {
        failed.error = "Xyce completed but did not produce " + prn.getFullPathName();
        return failed;
    }
    auto result = resultFromPrn(analysis, settings, prn, netlist, (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
    result.warnings.add("Backend: Xyce external process (" + configuredExecutable().getFullPathName() + ")");
    result.warnings.add("Netlist: " + cir.getFullPathName());
    return result;
}
}

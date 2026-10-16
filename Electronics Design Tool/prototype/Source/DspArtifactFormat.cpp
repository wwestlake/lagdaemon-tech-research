#include "DspArtifactFormat.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace dsp_artifact
{

namespace
{
std::uint64_t fnv1a64(const void* data, std::size_t sizeBytes, std::uint64_t seed = 14695981039346656037ULL)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < sizeBytes; ++i)
    {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex64(std::uint64_t v)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

std::string elementTypeToString(circuit_sim::Element::Type t)
{
    using T = circuit_sim::Element::Type;
    switch (t)
    {
        case T::Resistor:         return "Resistor";
        case T::Capacitor:        return "Capacitor";
        case T::Inductor:         return "Inductor";
        case T::VoltageSource:    return "VoltageSource";
        case T::CurrentSource:    return "CurrentSource";
        case T::Vcvs:             return "Vcvs";
        case T::Vccs:             return "Vccs";
        case T::Diode:            return "Diode";
        case T::OpAmp:            return "OpAmp";
        case T::VariableResistor: return "VariableResistor";
        case T::Switch:           return "Switch";
        default:                  return "Unsupported";
    }
}

bool stringToElementType(const std::string& s, circuit_sim::Element::Type& out)
{
    using T = circuit_sim::Element::Type;
    if (s == "Resistor")         { out = T::Resistor; return true; }
    if (s == "Capacitor")        { out = T::Capacitor; return true; }
    if (s == "Inductor")         { out = T::Inductor; return true; }
    if (s == "VoltageSource")    { out = T::VoltageSource; return true; }
    if (s == "CurrentSource")    { out = T::CurrentSource; return true; }
    if (s == "Vcvs")             { out = T::Vcvs; return true; }
    if (s == "Vccs")             { out = T::Vccs; return true; }
    if (s == "Diode")            { out = T::Diode; return true; }
    if (s == "OpAmp")            { out = T::OpAmp; return true; }
    if (s == "VariableResistor") { out = T::VariableResistor; return true; }
    if (s == "Switch")           { out = T::Switch; return true; }
    return false;
}

std::string computeArtifactDigest(const ExecutableArtifact& a)
{
    std::ostringstream oss;
    oss << a.magic << "|" << a.schemaVersion << "|" << a.status << "|"
        << a.packageId << "|" << a.productName << "|" << a.productVersion << "|"
        << a.pluginCode4CC << "|" << a.vst3ComponentUidHex << "|"
        << a.hostSchemaVersion << "|" << a.targetArch << "|" << a.objectFormat << "|"
        << a.effectTopology << "|" << a.slots.size() << "|" << a.effects.size() << "|"
        << a.parameters.size();
    for (const auto& s : a.slots)
    {
        oss << "|slot:" << s.slotId << ":" << s.objectHashHex << ":" << s.objectBytes.size()
            << ":" << s.model.workspaceSize << ":" << s.model.coeffSize();
    }
    for (const auto& p : a.parameters)
    {
        oss << "|param:" << p.id << ":" << p.kind << ":" << p.minValue << ":" << p.maxValue << ":" << p.defaultValue;
    }
    for (const auto& e : a.effects)
    {
        oss << "|eff:" << e.id << ":" << e.name << ":" << e.modelSlotIndex;
    }
    return computeStringHashHex(oss.str());
}

std::string prettifyParamName(const std::string& rawId)
{
    std::string out;
    out.reserve(rawId.size());
    bool capNext = true;
    for (char c : rawId)
    {
        if (c == '_' || c == '-')
        {
            out.push_back(' ');
            capNext = true;
        }
        else if (capNext)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            capNext = false;
        }
        else
        {
            out.push_back(c);
        }
    }
    return out.empty() ? "Control" : out;
}

} // namespace

std::string computeHashHex(const void* data, std::size_t sizeBytes)
{
    return hex64(fnv1a64(data, sizeBytes));
}

std::string computeStringHashHex(const std::string& text)
{
    return computeHashHex(text.data(), text.size());
}

std::string derivePluginCode4CC(const std::string& packageId)
{
    const std::uint64_t h = fnv1a64(packageId.data(), packageId.size());
    static constexpr const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static constexpr std::size_t kCount = sizeof(kChars) - 1;
    std::string code = "D";
    code.push_back(kChars[(h >> 8) % 26]); // Uppercase second char
    code.push_back(kChars[(h >> 20) % kCount]);
    code.push_back(kChars[(h >> 32) % kCount]);
    return code;
}

std::string deriveVst3UidHex(const std::string& packageId, const std::string& salt)
{
    const std::string keyA = "djehuti.vst3.uid.a:" + salt + ":" + packageId;
    const std::string keyB = "djehuti.vst3.uid.b:" + salt + ":" + packageId;
    return computeStringHashHex(keyA) + computeStringHashHex(keyB);
}

juce::var serializeCircuitToVar(const circuit_sim::Circuit& circuit, const audio_dsp::Config& cfg)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("nodeCount", circuit.nodeCount());
    root->setProperty("sampleRate", cfg.sampleRate);
    root->setProperty("audioInputElement", cfg.audioInputElement);
    root->setProperty("audioOutputNode", cfg.audioOutputNode);
    root->setProperty("inputVolts", cfg.inputVolts);
    root->setProperty("outputFullScaleVolts", cfg.outputFullScaleVolts);

    juce::Array<juce::var> elems;
    for (const auto& e : circuit.elements())
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty("type", juce::String(elementTypeToString(e.type)));
        eo->setProperty("name", juce::String(e.name));
        eo->setProperty("value", e.value);
        eo->setProperty("paramId", juce::String(e.paramId));
        eo->setProperty("isWiperToPin2", e.isWiperToPin2);

        juce::Array<juce::var> nodesArr;
        for (int n : e.nodes) nodesArr.add(n);
        eo->setProperty("nodes", nodesArr);

        eo->setProperty("waveKind", static_cast<int>(e.wave.kind));
        eo->setProperty("waveOffset", e.wave.offset);
        eo->setProperty("waveAmplitude", e.wave.amplitude);
        eo->setProperty("waveFrequency", e.wave.frequency);

        eo->setProperty("diodeIs", e.diode.saturationCurrent);
        eo->setProperty("diodeEmission", e.diode.emission);
        eo->setProperty("opampGain", e.opamp.gain);
        eo->setProperty("opampLimited", e.opamp.limited);
        elems.add(juce::var(eo));
    }
    root->setProperty("elements", elems);
    return juce::var(root);
}

bool deserializeCircuitFromVar(const juce::var& v,
                               circuit_sim::Circuit& outCircuit,
                               audio_dsp::Config& outCfg,
                               std::string& outError)
{
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        outError = "Serialized circuit is not a JSON object.";
        return false;
    }

    const int nodeCount = static_cast<int>(obj->getProperty("nodeCount"));
    if (nodeCount < 2)
    {
        outError = "Serialized circuit must have at least 2 nodes.";
        return false;
    }

    outCircuit = circuit_sim::Circuit();
    while (outCircuit.nodeCount() < nodeCount)
        outCircuit.addNode();

    outCfg.sampleRate = static_cast<double>(obj->getProperty("sampleRate"));
    if (outCfg.sampleRate <= 0.0) outCfg.sampleRate = 48000.0;
    outCfg.audioInputElement = static_cast<int>(obj->getProperty("audioInputElement"));
    outCfg.audioOutputNode = static_cast<int>(obj->getProperty("audioOutputNode"));
    outCfg.inputVolts = static_cast<double>(obj->getProperty("inputVolts"));
    if (outCfg.inputVolts == 0.0) outCfg.inputVolts = 1.0;
    outCfg.outputFullScaleVolts = static_cast<double>(obj->getProperty("outputFullScaleVolts"));
    if (outCfg.outputFullScaleVolts == 0.0) outCfg.outputFullScaleVolts = 1.0;

    const auto elemsVar = obj->getProperty("elements");
    if (!elemsVar.isArray())
    {
        outError = "Serialized circuit elements field is missing or not an array.";
        return false;
    }

    const auto* arr = elemsVar.getArray();
    if (arr == nullptr || arr->isEmpty())
    {
        outError = "Serialized circuit contains zero elements.";
        return false;
    }

    for (const auto& item : *arr)
    {
        const auto* eo = item.getDynamicObject();
        if (eo == nullptr)
        {
            outError = "Malformed element in serialized circuit.";
            return false;
        }
        circuit_sim::Element::Type type {};
        const std::string typeStr = eo->getProperty("type").toString().toStdString();
        if (!stringToElementType(typeStr, type))
        {
            outError = "Unsupported element type in serialized circuit: " + typeStr;
            return false;
        }

        const std::string name = eo->getProperty("name").toString().toStdString();
        const double value = static_cast<double>(eo->getProperty("value"));
        const std::string paramId = eo->getProperty("paramId").toString().toStdString();
        const bool isWiperToPin2 = static_cast<bool>(eo->getProperty("isWiperToPin2"));

        std::vector<int> nodes;
        if (const auto* na = eo->getProperty("nodes").getArray())
        {
            for (const auto& nv : *na)
                nodes.push_back(static_cast<int>(nv));
        }

        auto nodeAt = [&](std::size_t idx) -> int {
            return idx < nodes.size() ? nodes[idx] : 0;
        };

        using T = circuit_sim::Element::Type;
        switch (type)
        {
            case T::Resistor:
                outCircuit.addResistor(name, nodeAt(0), nodeAt(1), value);
                break;
            case T::Capacitor:
                outCircuit.addCapacitor(name, nodeAt(0), nodeAt(1), value);
                break;
            case T::Inductor:
                outCircuit.addInductor(name, nodeAt(0), nodeAt(1), value);
                break;
            case T::VoltageSource:
            case T::CurrentSource:
            {
                circuit_sim::Waveform w;
                w.kind = static_cast<circuit_sim::Waveform::Kind>(static_cast<int>(eo->getProperty("waveKind")));
                w.offset = static_cast<double>(eo->getProperty("waveOffset"));
                w.amplitude = static_cast<double>(eo->getProperty("waveAmplitude"));
                w.frequency = static_cast<double>(eo->getProperty("waveFrequency"));
                if (type == T::VoltageSource)
                    outCircuit.addVoltageSource(name, nodeAt(0), nodeAt(1), w);
                else
                    outCircuit.addCurrentSource(name, nodeAt(0), nodeAt(1), w);
                break;
            }
            case T::Vcvs:
                outCircuit.addVcvs(name, nodeAt(0), nodeAt(1), nodeAt(2), nodeAt(3), value);
                break;
            case T::Vccs:
                outCircuit.addVccs(name, nodeAt(0), nodeAt(1), nodeAt(2), nodeAt(3), value);
                break;
            case T::Diode:
            {
                circuit_sim::DiodeModel dm;
                const double is = static_cast<double>(eo->getProperty("diodeIs"));
                const double em = static_cast<double>(eo->getProperty("diodeEmission"));
                if (is > 0.0) dm.saturationCurrent = is;
                if (em > 0.0) dm.emission = em;
                outCircuit.addDiode(name, nodeAt(0), nodeAt(1), dm);
                break;
            }
            case T::OpAmp:
            {
                circuit_sim::OpAmpModel om;
                const double g = static_cast<double>(eo->getProperty("opampGain"));
                if (g > 0.0) om.gain = g;
                om.limited = static_cast<bool>(eo->getProperty("opampLimited"));
                outCircuit.addOpAmp(name, nodeAt(0), nodeAt(1), nodeAt(2), nodeAt(3), nodeAt(4), om);
                break;
            }
            case T::VariableResistor:
                outCircuit.addVariableResistor(name, nodeAt(0), nodeAt(1), value, paramId, isWiperToPin2);
                break;
            case T::Switch:
                outCircuit.addSwitch(name, nodeAt(0), nodeAt(1), paramId);
                break;
            default:
                outError = "Unsupported element type in circuit deserialization.";
                return false;
        }
    }
    return true;
}

BuildArtifactResult buildExecutableArtifact(const BuildArtifactInput& input)
{
    BuildArtifactResult result;
    if (input.circuits.empty())
    {
        result.error = "Cannot export VST3 artifact: no circuit slot provided.";
        return result;
    }

    ExecutableArtifact artifact;
    artifact.magic = kArtifactMagic;
    artifact.schemaVersion = kArtifactSchemaVersion;
    artifact.status = kExecutableStatus;
    artifact.packageId = input.packageId.empty() ? "djehuti_circuit_vst" : input.packageId;
    artifact.productName = input.productName.empty() ? "Djehuti Circuit Effect" : input.productName;
    artifact.productVersion = input.productVersion.empty() ? "1.0.0" : input.productVersion;
    artifact.manufacturerName = input.manufacturerName.empty() ? "Djehuti Audio" : input.manufacturerName;
    artifact.pluginCode4CC = derivePluginCode4CC(artifact.packageId);
    artifact.vst3ComponentUidHex = deriveVst3UidHex(artifact.packageId, "component");
    artifact.effectTopology = input.effectTopology.empty() ? "single" : input.effectTopology;

    // Compile each circuit slot to native x86_64 COFF object code
    for (std::size_t idx = 0; idx < input.circuits.size(); ++idx)
    {
        const auto& cSlot = input.circuits[idx];
        CompiledModelSlot compiledSlot;
        compiledSlot.slotId = cSlot.slotId.empty() ? ("slot_" + std::to_string(idx)) : cSlot.slotId;
        compiledSlot.slotName = cSlot.slotName.empty() ? ("Circuit " + std::to_string(idx + 1)) : cSlot.slotName;
        compiledSlot.circuit = cSlot.circuit;
        compiledSlot.dspConfig = cSlot.dspConfig;

        compiledSlot.model = audio_dsp::build(cSlot.circuit, cSlot.dspConfig);
        if (!compiledSlot.model.ok)
        {
            result.error = "Circuit DSP reduction failed for '" + compiledSlot.slotName + "': " + compiledSlot.model.error;
            return result;
        }

        const std::string frustSrc = compiledSlot.model.frustSource();
        const auto objCompile = frust_engine::compileToObject(compiledSlot.slotId, frustSrc);
        if (!objCompile.result.ok || objCompile.objectBytes.empty())
        {
            result.error = "FRust native object compilation failed for '" + compiledSlot.slotName + "': " + objCompile.result.report();
            return result;
        }

        compiledSlot.objectBytes = objCompile.objectBytes;
        compiledSlot.objectHashHex = computeHashHex(compiledSlot.objectBytes.data(), compiledSlot.objectBytes.size());
        compiledSlot.entryPoints = { "process_sample" };
        compiledSlot.requiredHostFunctions = audio_dsp::Model::requiredHostFunctions();
        compiledSlot.runtimeManifestJson = frust_engine::manifestJson(
            artifact.packageId + "_" + compiledSlot.slotId,
            "DSP model slot " + compiledSlot.slotName,
            compiledSlot.requiredHostFunctions,
            compiledSlot.entryPoints);

        if (input.retainDevelopmentSource)
        {
            compiledSlot.optionalFrustSource = frustSrc;
            compiledSlot.optionalLlvmIr = objCompile.optimizedIr;
        }

        artifact.slots.push_back(std::move(compiledSlot));
    }

    // Configure effect presets (1 or 3 effects)
    if (!input.effects.empty())
    {
        artifact.effects = input.effects;
    }
    else if (artifact.slots.size() == 3)
    {
        artifact.effectTopology = "three_independent";
        for (int i = 0; i < 3; ++i)
        {
            EffectPresetSlot eff;
            eff.id = "effect_" + std::to_string(i + 1);
            eff.name = artifact.slots[(std::size_t)i].slotName;
            eff.description = "Independent circuit DSP topology " + std::to_string(i + 1);
            eff.modelSlotIndex = i;
            artifact.effects.push_back(std::move(eff));
        }
    }
    else
    {
        EffectPresetSlot eff;
        eff.id = "effect_1";
        eff.name = artifact.productName;
        eff.description = "Primary circuit DSP model";
        eff.modelSlotIndex = 0;
        artifact.effects.push_back(std::move(eff));
    }

    // Validate effect slot references
    for (const auto& eff : artifact.effects)
    {
        if (eff.modelSlotIndex < 0 || eff.modelSlotIndex >= static_cast<int>(artifact.slots.size()))
        {
            result.error = "Effect preset '" + eff.name + "' references out-of-range modelSlotIndex " +
                           std::to_string(eff.modelSlotIndex);
            return result;
        }
    }

    // Build parameter schema
    if (!input.customParameters.empty())
    {
        artifact.parameters = input.customParameters;
    }
    else
    {
        // Collect switches controlled by 3-effect coupled mode overrides so we can mark them appropriately
        std::unordered_map<std::string, bool> modeDrivenSwitches;
        if (artifact.effects.size() >= 3 && artifact.slots.size() == 1)
        {
            for (const auto& eff : artifact.effects)
                for (const auto& kv : eff.livePortOverrides)
                    modeDrivenSwitches[kv.first] = true;
        }

        std::unordered_map<std::string, bool> seenIds;
        for (std::size_t sIdx = 0; sIdx < artifact.slots.size(); ++sIdx)
        {
            const auto& slot = artifact.slots[sIdx];
            for (const auto& lp : slot.model.livePorts)
            {
                if (seenIds[lp.paramId])
                    continue;
                seenIds[lp.paramId] = true;

                ParameterSpec p;
                p.id = lp.paramId;
                p.name = prettifyParamName(lp.paramId);
                p.livePortId = lp.paramId;
                p.slotIndex = artifact.slots.size() == 1 ? -1 : static_cast<int>(sIdx);
                p.baseResistance = lp.baseResistance;
                p.isWiperToPin2 = lp.isWiperToPin2;
                p.hiddenInModeSelector = modeDrivenSwitches[lp.paramId];

                if (lp.kind == audio_dsp::LivePort::Kind::Switch)
                {
                    p.kind = "switch";
                    p.unit = "";
                    p.minValue = 0.0;
                    p.maxValue = 1.0;
                    p.defaultValue = 0.0;
                    p.step = 1.0;
                }
                else
                {
                    p.kind = "potentiometer";
                    p.unit = "%";
                    p.minValue = 0.0;
                    p.maxValue = 1.0;
                    p.defaultValue = 0.5;
                    p.step = 0.0;
                }
                artifact.parameters.push_back(std::move(p));
            }
        }
    }

    artifact.artifactIntegrityHashHex = computeArtifactDigest(artifact);
    result.serializedJson = serializeArtifactToJson(artifact);
    result.artifact = std::move(artifact);
    result.ok = true;
    return result;
}

juce::String serializeArtifactToJson(const ExecutableArtifact& artifact)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("magic", juce::String(artifact.magic));
    root->setProperty("schemaVersion", artifact.schemaVersion);
    root->setProperty("status", juce::String(artifact.status));

    root->setProperty("packageId", juce::String(artifact.packageId));
    root->setProperty("productName", juce::String(artifact.productName));
    root->setProperty("productVersion", juce::String(artifact.productVersion));
    root->setProperty("manufacturerName", juce::String(artifact.manufacturerName));
    root->setProperty("pluginCode4CC", juce::String(artifact.pluginCode4CC));
    root->setProperty("vst3ComponentUidHex", juce::String(artifact.vst3ComponentUidHex));

    root->setProperty("hostSchemaVersion", artifact.hostSchemaVersion);
    root->setProperty("minHostSchemaVersion", artifact.minHostSchemaVersion);
    root->setProperty("maxHostSchemaVersion", artifact.maxHostSchemaVersion);
    root->setProperty("compilerVersion", juce::String(artifact.compilerVersion));
    root->setProperty("targetArch", juce::String(artifact.targetArch));
    root->setProperty("objectFormat", juce::String(artifact.objectFormat));
    root->setProperty("effectTopology", juce::String(artifact.effectTopology));

    juce::Array<juce::var> slotsArr;
    for (const auto& s : artifact.slots)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("slotId", juce::String(s.slotId));
        so->setProperty("slotName", juce::String(s.slotName));
        so->setProperty("workspaceSize", s.model.workspaceSize);
        so->setProperty("coeffSize", s.model.coeffSize());
        so->setProperty("stateCount", s.model.stateCount);
        so->setProperty("inputCount", s.model.inputCount);
        so->setProperty("portCount", s.model.portCount);
        so->setProperty("livePortCount", s.model.livePortCount);
        so->setProperty("sampleRate", s.model.sampleRate);
        so->setProperty("inputVolts", s.model.inputVolts);
        so->setProperty("outputFullScaleVolts", s.model.outputFullScaleVolts);

        juce::Array<juce::var> epArr;
        for (const auto& ep : s.entryPoints) epArr.add(juce::String(ep));
        so->setProperty("entryPoints", epArr);

        juce::Array<juce::var> hfArr;
        for (const auto& hf : s.requiredHostFunctions) hfArr.add(juce::String(hf));
        so->setProperty("requiredHostFunctions", hfArr);

        so->setProperty("runtimeManifestJson", juce::String(s.runtimeManifestJson));
        so->setProperty("objectByteSize", static_cast<juce::int64>(s.objectBytes.size()));
        so->setProperty("objectHashHex", juce::String(s.objectHashHex));
        so->setProperty("objectCodeBase64", juce::Base64::toBase64(s.objectBytes.data(), s.objectBytes.size()));
        so->setProperty("circuit", serializeCircuitToVar(s.circuit, s.dspConfig));

        if (!s.optionalFrustSource.empty())
            so->setProperty("optionalFrustSource", juce::String(s.optionalFrustSource));

        slotsArr.add(juce::var(so));
    }
    root->setProperty("slots", slotsArr);

    juce::Array<juce::var> effectsArr;
    for (const auto& e : artifact.effects)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty("id", juce::String(e.id));
        eo->setProperty("name", juce::String(e.name));
        eo->setProperty("description", juce::String(e.description));
        eo->setProperty("modelSlotIndex", e.modelSlotIndex);

        auto* ovObj = new juce::DynamicObject();
        for (const auto& kv : e.livePortOverrides)
            ovObj->setProperty(juce::Identifier(kv.first), kv.second);
        eo->setProperty("livePortOverrides", juce::var(ovObj));
        effectsArr.add(juce::var(eo));
    }
    root->setProperty("effects", effectsArr);

    juce::Array<juce::var> paramsArr;
    for (const auto& p : artifact.parameters)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty("id", juce::String(p.id));
        po->setProperty("name", juce::String(p.name));
        po->setProperty("unit", juce::String(p.unit));
        po->setProperty("kind", juce::String(p.kind));
        po->setProperty("minValue", p.minValue);
        po->setProperty("maxValue", p.maxValue);
        po->setProperty("defaultValue", p.defaultValue);
        po->setProperty("step", p.step);
        po->setProperty("slotIndex", p.slotIndex);
        po->setProperty("livePortId", juce::String(p.livePortId));
        po->setProperty("baseResistance", p.baseResistance);
        po->setProperty("isWiperToPin2", p.isWiperToPin2);
        po->setProperty("hiddenInModeSelector", p.hiddenInModeSelector);
        paramsArr.add(juce::var(po));
    }
    root->setProperty("parameters", paramsArr);

    const std::string digest = artifact.artifactIntegrityHashHex.empty()
                                   ? computeArtifactDigest(artifact)
                                   : artifact.artifactIntegrityHashHex;
    root->setProperty("artifactIntegrityHashHex", juce::String(digest));

    return juce::JSON::toString(juce::var(root), false);
}

LoadArtifactResult loadAndValidateArtifactFromJson(const juce::String& jsonText)
{
    LoadArtifactResult res;
    if (jsonText.trim().isEmpty())
    {
        res.error = "Artifact payload is empty.";
        return res;
    }

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(jsonText, parsed);
    if (parseResult.failed() || !parsed.isObject())
    {
        res.error = "Artifact is not valid JSON: " + parseResult.getErrorMessage().toStdString();
        return res;
    }

    const auto* root = parsed.getDynamicObject();
    const std::string status = root->getProperty("status").toString().toStdString();
    if (status == "source_ready_il_lowering_pending")
    {
        res.error = "Rejected non-executable placeholder .frust-il artifact (status='source_ready_il_lowering_pending').";
        return res;
    }

    ExecutableArtifact a;
    a.magic = root->getProperty("magic").toString().toStdString();
    if (a.magic != kArtifactMagic)
    {
        res.error = "Invalid artifact magic header (expected '" + std::string(kArtifactMagic) + "', got '" + a.magic + "').";
        return res;
    }

    a.schemaVersion = static_cast<int>(root->getProperty("schemaVersion"));
    if (a.schemaVersion != kArtifactSchemaVersion)
    {
        res.error = "Incompatible artifact schemaVersion " + std::to_string(a.schemaVersion) +
                    " (expected " + std::to_string(kArtifactSchemaVersion) + ").";
        return res;
    }

    a.status = status;
    if (a.status != kExecutableStatus)
    {
        res.error = "Artifact status is not executable ('" + a.status + "').";
        return res;
    }

    a.packageId = root->getProperty("packageId").toString().toStdString();
    a.productName = root->getProperty("productName").toString().toStdString();
    a.productVersion = root->getProperty("productVersion").toString().toStdString();
    a.manufacturerName = root->getProperty("manufacturerName").toString().toStdString();
    a.pluginCode4CC = root->getProperty("pluginCode4CC").toString().toStdString();
    a.vst3ComponentUidHex = root->getProperty("vst3ComponentUidHex").toString().toStdString();

    a.hostSchemaVersion = static_cast<int>(root->getProperty("hostSchemaVersion"));
    a.minHostSchemaVersion = static_cast<int>(root->getProperty("minHostSchemaVersion"));
    a.maxHostSchemaVersion = static_cast<int>(root->getProperty("maxHostSchemaVersion"));
    if (a.hostSchemaVersion < 1 || a.minHostSchemaVersion > 1 || a.maxHostSchemaVersion < 1)
    {
        res.error = "Incompatible runtime hostSchemaVersion in artifact.";
        return res;
    }

    a.compilerVersion = root->getProperty("compilerVersion").toString().toStdString();
    a.targetArch = root->getProperty("targetArch").toString().toStdString();
    a.objectFormat = root->getProperty("objectFormat").toString().toStdString();
    if (a.targetArch != kTargetArch || a.objectFormat != kObjectFormat)
    {
        res.error = "Incompatible target architecture/object format ('" + a.targetArch + "' / '" + a.objectFormat + "').";
        return res;
    }

    a.effectTopology = root->getProperty("effectTopology").toString().toStdString();

    const auto* slotsArr = root->getProperty("slots").getArray();
    if (slotsArr == nullptr || slotsArr->isEmpty())
    {
        res.error = "Artifact contains no compiled DSP slots.";
        return res;
    }

    for (const auto& sv : *slotsArr)
    {
        const auto* so = sv.getDynamicObject();
        if (so == nullptr)
        {
            res.error = "Malformed slot entry in artifact.";
            return res;
        }

        CompiledModelSlot slot;
        slot.slotId = so->getProperty("slotId").toString().toStdString();
        slot.slotName = so->getProperty("slotName").toString().toStdString();
        slot.runtimeManifestJson = so->getProperty("runtimeManifestJson").toString().toStdString();
        slot.objectHashHex = so->getProperty("objectHashHex").toString().toStdString();
        const auto expectedSize = static_cast<std::size_t>(static_cast<juce::int64>(so->getProperty("objectByteSize")));

        slot.entryPoints.clear();
        if (const auto* epArr = so->getProperty("entryPoints").getArray())
            for (const auto& ep : *epArr) slot.entryPoints.push_back(ep.toString().toStdString());

        if (std::find(slot.entryPoints.begin(), slot.entryPoints.end(), "process_sample") == slot.entryPoints.end())
        {
            res.error = "Slot '" + slot.slotId + "' is missing required entry point 'process_sample'.";
            return res;
        }

        slot.requiredHostFunctions.clear();
        if (const auto* hfArr = so->getProperty("requiredHostFunctions").getArray())
            for (const auto& hf : *hfArr) slot.requiredHostFunctions.push_back(hf.toString().toStdString());

        const juce::String b64 = so->getProperty("objectCodeBase64").toString();
        if (b64.isEmpty())
        {
            res.error = "Slot '" + slot.slotId + "' has empty objectCodeBase64.";
            return res;
        }

        juce::MemoryOutputStream decoded;
        if (!juce::Base64::convertFromBase64(decoded, b64) || decoded.getDataSize() == 0)
        {
            res.error = "Slot '" + slot.slotId + "' contains invalid base64 object code.";
            return res;
        }

        const auto* rawBytes = static_cast<const std::uint8_t*>(decoded.getData());
        slot.objectBytes.assign(rawBytes, rawBytes + decoded.getDataSize());
        if (slot.objectBytes.size() != expectedSize)
        {
            res.error = "Slot '" + slot.slotId + "' object byte size mismatch (expected " +
                        std::to_string(expectedSize) + ", got " + std::to_string(slot.objectBytes.size()) + ").";
            return res;
        }

        const std::string actualObjHash = computeHashHex(slot.objectBytes.data(), slot.objectBytes.size());
        if (actualObjHash != slot.objectHashHex)
        {
            res.error = "Slot '" + slot.slotId + "' object code integrity hash mismatch (expected " +
                        slot.objectHashHex + ", got " + actualObjHash + ").";
            return res;
        }

        std::string circuitErr;
        if (!deserializeCircuitFromVar(so->getProperty("circuit"), slot.circuit, slot.dspConfig, circuitErr))
        {
            res.error = "Slot '" + slot.slotId + "' circuit deserialization failed: " + circuitErr;
            return res;
        }

        slot.model = audio_dsp::build(slot.circuit, slot.dspConfig);
        if (!slot.model.ok)
        {
            res.error = "Slot '" + slot.slotId + "' model reconstruction failed: " + slot.model.error;
            return res;
        }

        const int expectedWs = static_cast<int>(so->getProperty("workspaceSize"));
        const int expectedCoeff = static_cast<int>(so->getProperty("coeffSize"));
        if (slot.model.workspaceSize != expectedWs || slot.model.coeffSize() != expectedCoeff)
        {
            res.error = "Slot '" + slot.slotId + "' workspace/coefficient dimension mismatch against compiled ABI.";
            return res;
        }

        slot.optionalFrustSource = so->getProperty("optionalFrustSource").toString().toStdString();
        a.slots.push_back(std::move(slot));
    }

    if (const auto* effArr = root->getProperty("effects").getArray())
    {
        for (const auto& ev : *effArr)
        {
            const auto* eo = ev.getDynamicObject();
            if (eo == nullptr) continue;
            EffectPresetSlot eff;
            eff.id = eo->getProperty("id").toString().toStdString();
            eff.name = eo->getProperty("name").toString().toStdString();
            eff.description = eo->getProperty("description").toString().toStdString();
            eff.modelSlotIndex = static_cast<int>(eo->getProperty("modelSlotIndex"));
            if (eff.modelSlotIndex < 0 || eff.modelSlotIndex >= static_cast<int>(a.slots.size()))
            {
                res.error = "Effect '" + eff.name + "' references invalid modelSlotIndex.";
                return res;
            }
            if (const auto* ovObj = eo->getProperty("livePortOverrides").getDynamicObject())
            {
                for (const auto& prop : ovObj->getProperties())
                    eff.livePortOverrides[prop.name.toString().toStdString()] = static_cast<double>(prop.value);
            }
            a.effects.push_back(std::move(eff));
        }
    }

    if (const auto* paramsArr = root->getProperty("parameters").getArray())
    {
        for (const auto& pv : *paramsArr)
        {
            const auto* po = pv.getDynamicObject();
            if (po == nullptr) continue;
            ParameterSpec p;
            p.id = po->getProperty("id").toString().toStdString();
            p.name = po->getProperty("name").toString().toStdString();
            p.unit = po->getProperty("unit").toString().toStdString();
            p.kind = po->getProperty("kind").toString().toStdString();
            p.minValue = static_cast<double>(po->getProperty("minValue"));
            p.maxValue = static_cast<double>(po->getProperty("maxValue"));
            p.defaultValue = static_cast<double>(po->getProperty("defaultValue"));
            p.step = static_cast<double>(po->getProperty("step"));
            p.slotIndex = static_cast<int>(po->getProperty("slotIndex"));
            p.livePortId = po->getProperty("livePortId").toString().toStdString();
            p.baseResistance = static_cast<double>(po->getProperty("baseResistance"));
            p.isWiperToPin2 = static_cast<bool>(po->getProperty("isWiperToPin2"));
            p.hiddenInModeSelector = static_cast<bool>(po->getProperty("hiddenInModeSelector"));
            a.parameters.push_back(std::move(p));
        }
    }

    a.artifactIntegrityHashHex = root->getProperty("artifactIntegrityHashHex").toString().toStdString();
    const std::string computedDigest = computeArtifactDigest(a);
    if (a.artifactIntegrityHashHex.empty() || a.artifactIntegrityHashHex != computedDigest)
    {
        res.error = "Artifact integrity hash mismatch (expected " + computedDigest +
                    ", got " + a.artifactIntegrityHashHex + ").";
        return res;
    }

    res.artifact = std::move(a);
    res.ok = true;
    return res;
}

LoadArtifactResult loadAndValidateArtifactFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        LoadArtifactResult r;
        r.error = "Artifact file does not exist: " + file.getFullPathName().toStdString();
        return r;
    }
    return loadAndValidateArtifactFromJson(file.loadFileAsString());
}

} // namespace dsp_artifact

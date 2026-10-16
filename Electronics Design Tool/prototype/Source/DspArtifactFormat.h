#pragma once

#include "AudioDsp.h"
#include "CircuitSolver.h"
#include "FrustEngine.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsp_artifact
{

inline constexpr const char* kArtifactMagic = "DJEHUTI_FRUST_EXECUTABLE_IL";
inline constexpr int kArtifactSchemaVersion = 2;
inline constexpr const char* kExecutableStatus = "executable_object_ready";
inline constexpr const char* kTargetArch = "x86_64-pc-windows-msvc";
inline constexpr const char* kObjectFormat = "COFF-x86_64";
inline constexpr const char* kCompilerVersion = "0.1.0";

struct ParameterSpec
{
    std::string id;            // Stable automation ID (e.g., "POT1_wiper", "SW1_state", "drive")
    std::string name;          // Human-readable label (e.g., "Drive", "Tone", "Clip Mode")
    std::string unit;          // e.g., "%", "kOhm", "dB", ""
    std::string kind;          // "potentiometer", "switch", "choice", "gain_db", "mix"
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.5;
    double step = 0.0;         // 0 = continuous, 1 = discrete step
    int slotIndex = -1;        // -1 = shared across slots, >=0 = bound to specific slot
    std::string livePortId;    // Maps to LivePort::paramId in audio_dsp::Model
    double baseResistance = 1000.0;
    bool isWiperToPin2 = false;
    bool hiddenInModeSelector = false; // True when a switch is controlled by the 3-effect selector
};

struct EffectPresetSlot
{
    std::string id;            // e.g., "effect_1", "effect_2", "effect_3"
    std::string name;          // e.g., "Silicon Symmetrical", "LED Asymmetrical", "Clean Boost"
    std::string description;
    int modelSlotIndex = 0;    // Index into ExecutableArtifact::slots (0 for coupled single-model, 0..2 for multi-model)
    // Live-port overrides applied when this effect is selected (used for coupled-circuit 3-effect mode switching)
    std::unordered_map<std::string, double> livePortOverrides;
};

struct CompiledModelSlot
{
    std::string slotId = "slot_0";
    std::string slotName = "Primary Circuit";

    // Circuit & discretization config for exact sample-rate adaptation in prepareToPlay
    circuit_sim::Circuit circuit;
    audio_dsp::Config dspConfig;

    // Reference model built at export sample rate
    audio_dsp::Model model;

    // Precompiled native COFF x86_64 object code & runtime manifest
    std::vector<std::uint8_t> objectBytes;
    std::string objectHashHex;
    std::string runtimeManifestJson;
    std::vector<std::string> entryPoints { "process_sample" };
    std::vector<std::string> requiredHostFunctions { "djehuti_dsp_exp", "djehuti_dsp_log" };

    // Optional development reference (never compiled at plugin runtime)
    std::string optionalFrustSource;
    std::string optionalLlvmIr;
};

struct ExecutableArtifact
{
    std::string magic = kArtifactMagic;
    int schemaVersion = kArtifactSchemaVersion;
    std::string status = kExecutableStatus;

    // Product identity
    std::string packageId = "djehuti_circuit_vst";
    std::string productName = "Djehuti Circuit Effect";
    std::string productVersion = "1.0.0";
    std::string manufacturerName = "Djehuti Audio";
    std::string pluginCode4CC = "DjCe"; // Deterministic 4-char code
    std::string vst3ComponentUidHex;    // 32-hex-char deterministic GUID

    // Compiler/runtime compatibility
    int hostSchemaVersion = 1;
    int minHostSchemaVersion = 1;
    int maxHostSchemaVersion = 1;
    std::string compilerVersion = kCompilerVersion;
    std::string targetArch = kTargetArch;
    std::string objectFormat = kObjectFormat;

    // Effect topology:
    // - "single": 1 effect (1 slot, 1 preset)
    // - "three_coupled": 3 selectable effects sharing 1 coupled circuit slot via livePortOverrides
    // - "three_independent": 3 selectable effects backed by 3 compiled model slots
    std::string effectTopology = "single";

    std::vector<CompiledModelSlot> slots;
    std::vector<EffectPresetSlot> effects;
    std::vector<ParameterSpec> parameters;

    std::string artifactIntegrityHashHex;
};

struct BuildArtifactInput
{
    std::string packageId = "djehuti_circuit_vst";
    std::string productName = "Djehuti Circuit Effect";
    std::string productVersion = "1.0.0";
    std::string manufacturerName = "Djehuti Audio";
    std::string effectTopology = "single"; // "single", "three_coupled", "three_independent"

    struct CircuitInputSlot
    {
        std::string slotId = "slot_0";
        std::string slotName = "Primary Circuit";
        circuit_sim::Circuit circuit;
        audio_dsp::Config dspConfig;
    };

    std::vector<CircuitInputSlot> circuits;
    std::vector<EffectPresetSlot> effects;
    std::vector<ParameterSpec> customParameters; // If empty, auto-extracted from circuit LivePorts
    bool retainDevelopmentSource = true;
};

struct BuildArtifactResult
{
    bool ok = false;
    std::string error;
    ExecutableArtifact artifact;
    juce::String serializedJson;
};

struct LoadArtifactResult
{
    bool ok = false;
    std::string error;
    ExecutableArtifact artifact;
};

// Deterministic 64-bit FNV-1a hash formatted as 16 lowercase hex digits
std::string computeHashHex(const void* data, std::size_t sizeBytes);
std::string computeStringHashHex(const std::string& text);

// Deterministic 4-character plugin code and 32-hex-char VST3 UID from packageId
std::string derivePluginCode4CC(const std::string& packageId);
std::string deriveVst3UidHex(const std::string& packageId, const std::string& salt = "component");

// Serializes / deserializes circuit_sim::Circuit to/from juce::var for sample-rate re-discretization
juce::var serializeCircuitToVar(const circuit_sim::Circuit& circuit, const audio_dsp::Config& cfg);
bool deserializeCircuitFromVar(const juce::var& v, circuit_sim::Circuit& outCircuit, audio_dsp::Config& outCfg, std::string& outError);

// Compiles the circuit(s) to FRust native x64 COFF object code and packages the executable .frust-il artifact
BuildArtifactResult buildExecutableArtifact(const BuildArtifactInput& input);

// Serializes an ExecutableArtifact to JSON text (.frust-il)
juce::String serializeArtifactToJson(const ExecutableArtifact& artifact);

// Loads and strictly validates a .frust-il JSON text or file (rejects placeholders, corrupt hashes, wrong arch)
LoadArtifactResult loadAndValidateArtifactFromJson(const juce::String& jsonText);
LoadArtifactResult loadAndValidateArtifactFromFile(const juce::File& file);

} // namespace dsp_artifact

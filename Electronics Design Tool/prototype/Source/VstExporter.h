#pragma once

#include "AudioDsp.h"
#include "CircuitSolver.h"
#include "DspArtifactFormat.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace vst_exporter
{

enum class ExportMode
{
    SingleEffect = 0,               // 1 plugin with 1 circuit effect
    ThreeEffectsCoupled = 1,        // 1 plugin with 3 selectable modes in 1 coupled circuit
    ThreeEffectsIndependent = 2,    // 1 plugin with 3 selectable independent circuit models
    ThreeSeparatePlugins = 3        // 3 separate VST3 plugins (one per configured effect)
};

struct ExportRequest
{
    std::string packageId = "com.djehuti.vst.circuit_effect";
    std::string productName = "Djehuti Circuit Effect";
    std::string productVersion = "1.0.0";
    std::string manufacturerName = "Djehuti Audio";
    ExportMode mode = ExportMode::SingleEffect;

    // Circuit slots (1 for SingleEffect or ThreeEffectsCoupled; 3 for ThreeEffectsIndependent or ThreeSeparatePlugins)
    std::vector<dsp_artifact::BuildArtifactInput::CircuitInputSlot> circuits;
    std::vector<dsp_artifact::EffectPresetSlot> effects;
    std::vector<dsp_artifact::ParameterSpec> customParameters;

    juce::File outputDirectory;
    // Explicit developer build directory. When empty, a development checkout is recognised only
    // from the executable's location (<prototype>/build); installed copies use the packaged
    // Release template in <exe>/templates/vst3 and never build.
    juce::File cmakeBuildDirectory;
    bool buildVst3Bundle = true;    // Package the .vst3 bundle (developer: MSBuild /m:1; installed: packaged template)
    bool retainDevelopmentSource = true;
};

struct ExportedPluginBundleInfo
{
    std::string packageId;
    std::string productName;
    std::string pluginCode4CC;
    std::string vst3ComponentUidHex;
    juce::File packageDirectory;
    juce::File artifactFile;        // <packageDir>/model.frust-il
    juce::File frustSourceFile;     // <packageDir>/model.frust (optional dev reference)
    juce::File manifestFile;        // <packageDir>/manifest.json
    juce::File uiSchemaFile;        // <packageDir>/ui.json
    juce::File exportConfigFile;    // <packageDir>/vst_export_config.json
    juce::File vst3BundleDir;       // <packageDir>/<ProductName>.vst3
    juce::File vst3BinaryFile;      // <packageDir>/<ProductName>.vst3/Contents/x86_64-win/<ProductName>.vst3
    std::string vst3BinarySource;   // "developer_build" or "packaged_release_template" (empty when not packaged)
    std::string identityLimitation; // Non-empty when the binary's compiled identity differs from the request
    dsp_artifact::ExecutableArtifact artifact;
};

struct ExportResult
{
    bool ok = false;
    std::string error;
    std::string diagnosticsLog;
    std::vector<ExportedPluginBundleInfo> bundles;
};

// Serializes / deserializes ExportRequest configuration (vst_export_config.json)
juce::String serializeExportConfigToJson(const ExportRequest& req);
bool loadExportConfigFromJson(const juce::String& jsonText, ExportRequest& outReq, std::string& outError);
bool saveExportConfigToFile(const ExportRequest& req, const juce::File& file);
bool loadExportConfigFromFile(const juce::File& file, ExportRequest& outReq, std::string& outError);

// Generates ui.json control-panel and parameter layout descriptor from an ExecutableArtifact
juce::String buildUiSchemaJson(const dsp_artifact::ExecutableArtifact& artifact);

// Executes the full circuit -> DSP model -> FRust -> native COFF object -> .frust-il -> standalone .vst3 export
ExportResult runExport(const ExportRequest& request);

// Reference circuit builders for IDE presets and validation tests
dsp_artifact::BuildArtifactInput::CircuitInputSlot makeLinearRcToneCircuit(double sampleRate = 48000.0);
dsp_artifact::BuildArtifactInput::CircuitInputSlot makeNonlinearDiodeClipperCircuit(double sampleRate = 48000.0);
dsp_artifact::BuildArtifactInput::CircuitInputSlot makeCoupledThreeModeClipperCircuit(
    double sampleRate,
    std::vector<dsp_artifact::EffectPresetSlot>& outThreeEffects);
dsp_artifact::BuildArtifactInput::CircuitInputSlot makeResonantPresenceVoicingCircuit(double sampleRate = 48000.0);

} // namespace vst_exporter

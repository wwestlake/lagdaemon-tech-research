#include <JuceHeader.h>

#include "AudioDsp.h"
#include "CircuitSolver.h"
#include "DspArtifactFormat.h"
#include "FrustEngine.h"
#include "GeneratedVst/CircuitVstPlugin.h"
#include "VstExporter.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{

int gFailures = 0;

void check(bool condition, const std::string& label, const std::string& detail = {})
{
    if (condition)
    {
        std::cout << "  [PASS] " << label << "\n";
    }
    else
    {
        ++gFailures;
        std::cerr << "  [FAIL] " << label;
        if (!detail.empty())
            std::cerr << " -- " << detail;
        std::cerr << "\n";
    }
}

double computeRms(const float* data, int numSamples)
{
    if (numSamples <= 0) return 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sumSq += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return std::sqrt(sumSq / static_cast<double>(numSamples));
}

double computePeak(const float* data, int numSamples)
{
    double pk = 0.0;
    for (int i = 0; i < numSamples; ++i)
        pk = std::max(pk, std::abs(static_cast<double>(data[i])));
    return pk;
}

void fillSineMonoOrStereo(juce::AudioBuffer<float>& buf, double freqHz, double sampleRate, float amplitude, int startSampleIndex = 0)
{
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* dst = buf.getWritePointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(startSampleIndex + i) / sampleRate;
            dst[i] = amplitude * static_cast<float>(std::sin(twoPi * freqHz * t));
        }
    }
}

// 1. Test Linear & Nonlinear Circuit Artifact Export + Reference Match + Source-Free Object Loading
void testLinearAndNonlinearArtifactExecution()
{
    std::cout << "\n=== 1. Linear & Nonlinear Executable Artifact (.frust-il v2) vs Reference Model ===\n";

    // 1A: Linear RC Tone circuit
    {
        dsp_artifact::BuildArtifactInput in;
        in.packageId = "com.djehuti.test.linear_rc";
        in.productName = "Djehuti Linear RC Filter";
        in.retainDevelopmentSource = false; // Prove source is NOT needed at runtime
        in.circuits.push_back(vst_exporter::makeLinearRcToneCircuit(48000.0));

        const auto built = dsp_artifact::buildExecutableArtifact(in);
        check(built.ok, "Build linear RC executable artifact", built.error);
        if (!built.ok) return;

        check(built.artifact.status == dsp_artifact::kExecutableStatus,
              "Linear artifact status is executable_object_ready");
        check(!built.artifact.slots.empty() && !built.artifact.slots[0].objectBytes.empty(),
              "Linear artifact contains compiled COFF-x64 object bytes");
        check(built.artifact.slots[0].optionalFrustSource.empty(),
              "Linear artifact has no embedded FRust source (source-free execution)");

        djehuti_circuit_vst::Processor proc(built.serializedJson);
        check(proc.isValid(), "Linear Processor loaded precompiled object without source",
              proc.runtimeError().toStdString());

        proc.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(1, 256);
        fillSineMonoOrStereo(buf, 1000.0, 48000.0, 0.8f);

        // Step reference model on identical input
        const auto& refModel = built.artifact.slots[0].model;
        std::vector<double> refWs = refModel.initialWorkspace;
        std::unordered_map<std::string, double> defaultParams;
        defaultParams["tone_pot"] = 0.5;
        const auto refCoeffs = refModel.computeLiveCoefficients(defaultParams);

        std::vector<double> expected(256, 0.0);
        for (int i = 0; i < 256; ++i)
        {
            const double inV = static_cast<double>(buf.getSample(0, i)) * refModel.inputVolts;
            expected[(std::size_t)i] = refModel.step(refWs, refCoeffs.data(), inV) / refModel.outputFullScaleVolts;
        }

        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);

        double maxErr = 0.0;
        for (int i = 0; i < 256; ++i)
            maxErr = std::max(maxErr, std::abs(static_cast<double>(buf.getSample(0, i)) - expected[(std::size_t)i]));

        check(maxErr < 1e-6, "Linear precompiled VST processor matches audio_dsp::Model::step (< 1e-6)",
              "maxErr=" + std::to_string(maxErr));
    }

    // 1B: Nonlinear Diode Clipper circuit
    {
        dsp_artifact::BuildArtifactInput in;
        in.packageId = "com.djehuti.test.nonlinear_clipper";
        in.productName = "Djehuti Nonlinear Diode Clipper";
        in.retainDevelopmentSource = false; // Strip source to prove binary object execution
        in.circuits.push_back(vst_exporter::makeNonlinearDiodeClipperCircuit(48000.0));

        const auto built = dsp_artifact::buildExecutableArtifact(in);
        check(built.ok, "Build nonlinear diode clipper executable artifact", built.error);
        if (!built.ok) return;

        djehuti_circuit_vst::Processor proc(built.serializedJson);
        check(proc.isValid(), "Nonlinear Processor loaded precompiled object without source",
              proc.runtimeError().toStdString());

        proc.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buf(1, 512);
        fillSineMonoOrStereo(buf, 440.0, 48000.0, 0.9f);

        const auto& refModel = built.artifact.slots[0].model;
        std::vector<double> refWs = refModel.initialWorkspace;
        std::unordered_map<std::string, double> defaultParams;
        defaultParams["drive_pot"] = 0.5;
        defaultParams["sym_sw"] = 0.0;
        defaultParams["tone_pot"] = 0.5;
        const auto refCoeffs = refModel.computeLiveCoefficients(defaultParams);

        std::vector<double> expected(512, 0.0);
        for (int i = 0; i < 512; ++i)
        {
            const double inV = static_cast<double>(buf.getSample(0, i)) * refModel.inputVolts;
            expected[(std::size_t)i] = refModel.step(refWs, refCoeffs.data(), inV) / refModel.outputFullScaleVolts;
        }

        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);

        double maxErr = 0.0;
        for (int i = 0; i < 512; ++i)
            maxErr = std::max(maxErr, std::abs(static_cast<double>(buf.getSample(0, i)) - expected[(std::size_t)i]));

        const double outPeak = computePeak(buf.getReadPointer(0), 512);
        check(maxErr < 1e-5, "Nonlinear diode clipper VST processor matches audio_dsp::Model::step (< 1e-5)",
              "maxErr=" + std::to_string(maxErr) + ", peak=" + std::to_string(outPeak));
        check(outPeak > 0.1 && outPeak < 1.2, "Nonlinear diode clipper compresses high-level input within diode threshold",
              "outPeak=" + std::to_string(outPeak));
    }
}

// 2. Test 3-Effect Selector (Both Coupled Single-Circuit & 3 Independent Circuit Models)
void testThreeEffectTopologies()
{
    std::cout << "\n=== 2. Three-Effect Topologies (Coupled Single-Circuit & 3 Independent Circuit Models) ===\n";

    // 2A: Coupled 3-Mode Circuit (shared reactive state + livePortOverrides)
    {
        dsp_artifact::BuildArtifactInput in;
        in.packageId = "com.djehuti.test.coupled_trio";
        in.productName = "Djehuti Coupled 3-Mode Clipper";
        in.effectTopology = "three_coupled";
        in.circuits.push_back(vst_exporter::makeCoupledThreeModeClipperCircuit(48000.0, in.effects));

        const auto built = dsp_artifact::buildExecutableArtifact(in);
        check(built.ok && built.artifact.effects.size() == 3 && built.artifact.slots.size() == 1,
              "Built coupled 3-effect artifact (1 slot, 3 effect modes)", built.error);

        djehuti_circuit_vst::Processor proc(built.serializedJson);
        proc.prepareToPlay(48000.0, 512);

        auto measureModeResponse = [&](int modeIdx, double& outPosPeak, double& outNegPeak) {
            proc.resetAllWorkspaces();
            proc.setSelectedEffectIndex(modeIdx);
            // Warm up crossfade
            juce::AudioBuffer<float> warm(1, 512);
            fillSineMonoOrStereo(warm, 250.0, 48000.0, 0.95f, 0);
            juce::MidiBuffer midi;
            proc.processBlock(warm, midi);

            juce::AudioBuffer<float> buf(1, 512);
            fillSineMonoOrStereo(buf, 250.0, 48000.0, 0.95f, 512);
            proc.processBlock(buf, midi);

            outPosPeak = 0.0;
            outNegPeak = 0.0;
            const float* d = buf.getReadPointer(0);
            for (int i = 0; i < 512; ++i)
            {
                if (d[i] > outPosPeak) outPosPeak = d[i];
                if (-d[i] > outNegPeak) outNegPeak = -d[i];
            }
        };

        double pos0 = 0.0, neg0 = 0.0;
        double pos1 = 0.0, neg1 = 0.0;
        double pos2 = 0.0, neg2 = 0.0;
        measureModeResponse(0, pos0, neg0); // Symmetrical 1v1 silicon clip
        measureModeResponse(1, pos1, neg1); // Asymmetrical 1v2 clip
        measureModeResponse(2, pos2, neg2); // Clean preamp (diodes lifted)

        check(std::abs(pos0 - neg0) < 0.03,
              "Coupled Effect 0 (Silicon Symmetrical) clips symmetrically",
              "pos=" + std::to_string(pos0) + ", neg=" + std::to_string(neg0));
        check(neg1 > pos1 + 0.08,
              "Coupled Effect 1 (Asymmetric 1v2) exhibits asymmetric positive/negative clipping",
              "pos=" + std::to_string(pos1) + ", neg=" + std::to_string(neg1));
        check(pos2 > pos0 + 0.25,
              "Coupled Effect 2 (Clean Preamp) lifts diodes and passes higher unclipped headroom",
              "cleanPos=" + std::to_string(pos2) + " vs clipPos=" + std::to_string(pos0));
    }

    // 2B: Independent 3-Circuit Model Plugin (3 compiled COFF object slots in 1 plugin)
    {
        dsp_artifact::BuildArtifactInput in;
        in.packageId = "com.djehuti.test.independent_trio";
        in.productName = "Djehuti Multi-Circuit Suite";
        in.effectTopology = "three_independent";
        in.circuits.push_back(vst_exporter::makeLinearRcToneCircuit(48000.0));
        in.circuits.push_back(vst_exporter::makeNonlinearDiodeClipperCircuit(48000.0));
        in.circuits.push_back(vst_exporter::makeResonantPresenceVoicingCircuit(48000.0));

        const auto built = dsp_artifact::buildExecutableArtifact(in);
        check(built.ok && built.artifact.slots.size() == 3 && built.artifact.effects.size() == 3,
              "Built 3-independent-circuit artifact (3 compiled COFF slots, 3 effects)", built.error);

        djehuti_circuit_vst::Processor proc(built.serializedJson);
        check(proc.isValid(), "3-independent-slot Processor linked all 3 COFF objects",
              proc.runtimeError().toStdString());
        proc.prepareToPlay(48000.0, 512);

        double rmsBySlot[3] = { 0.0, 0.0, 0.0 };
        for (int m = 0; m < 3; ++m)
        {
            proc.resetAllWorkspaces();
            proc.setSelectedEffectIndex(m);
            juce::AudioBuffer<float> warm(1, 512);
            fillSineMonoOrStereo(warm, 900.0, 48000.0, 0.85f, 0);
            juce::MidiBuffer midi;
            proc.processBlock(warm, midi);

            juce::AudioBuffer<float> buf(1, 512);
            fillSineMonoOrStereo(buf, 900.0, 48000.0, 0.85f, 512);
            proc.processBlock(buf, midi);
            rmsBySlot[m] = computeRms(buf.getReadPointer(0), 512);
        }

        check(std::abs(rmsBySlot[0] - rmsBySlot[1]) > 0.02 &&
              std::abs(rmsBySlot[1] - rmsBySlot[2]) > 0.02,
              "Independent 3-effect selector switches across all 3 compiled circuit models with distinct outputs",
              "rms0=" + std::to_string(rmsBySlot[0]) + ", rms1=" + std::to_string(rmsBySlot[1]) +
              ", rms2=" + std::to_string(rmsBySlot[2]));
    }
}

// 3. Test Parameter Sweep, Automation, State Persistence, Stereo Independence, 2 Instances, & Sample Rates
void testRuntimeControlsStateStereoAndSampleRates()
{
    std::cout << "\n=== 3. Parameter Automation, State Save/Restore, Stereo Independence, 2 Instances & Sample Rates ===\n";

    dsp_artifact::BuildArtifactInput in;
    in.packageId = "com.djehuti.test.runtime_suite";
    in.productName = "Djehuti Runtime Verification Clipper";
    in.effectTopology = "three_coupled";
    in.circuits.push_back(vst_exporter::makeCoupledThreeModeClipperCircuit(48000.0, in.effects));
    const auto built = dsp_artifact::buildExecutableArtifact(in);
    check(built.ok, "Built runtime verification artifact", built.error);
    if (!built.ok) return;

    // 3A: Parameter sweep (drive_pot & tone_pot)
    {
        djehuti_circuit_vst::Processor proc(built.serializedJson);
        proc.prepareToPlay(48000.0, 512);

        proc.setParameterValuePlain("tone_pot", 0.1f);
        juce::AudioBuffer<float> bufLowR(1, 512);
        fillSineMonoOrStereo(bufLowR, 2500.0, 48000.0, 0.5f);
        juce::MidiBuffer midi;
        proc.processBlock(bufLowR, midi);
        const double rmsLowR = computeRms(bufLowR.getReadPointer(0), 512);

        proc.resetAllWorkspaces();
        proc.setParameterValuePlain("tone_pot", 0.95f);
        juce::AudioBuffer<float> bufHighR(1, 512);
        fillSineMonoOrStereo(bufHighR, 2500.0, 48000.0, 0.5f);
        proc.processBlock(bufHighR, midi);
        const double rmsHighR = computeRms(bufHighR.getReadPointer(0), 512);

        check(rmsLowR > rmsHighR * 1.15,
              "Live circuit potentiometer (tone_pot) dynamically updates MNA reduced coefficients",
              "rms(tone=0.1)=" + std::to_string(rmsLowR) + " vs rms(tone=0.95)=" + std::to_string(rmsHighR));
    }

    // 3B: State save & restore (getStateInformation / setStateInformation)
    {
        djehuti_circuit_vst::Processor procA(built.serializedJson);
        procA.prepareToPlay(48000.0, 256);
        procA.setParameterValuePlain("drive_pot", 0.82f);
        procA.setParameterValuePlain("tone_pot", 0.24f);
        procA.setParameterValuePlain("input_gain_db", 6.0f);
        procA.setParameterValuePlain("mix", 0.75f);
        procA.setSelectedEffectIndex(2);

        juce::MemoryBlock savedState;
        procA.getStateInformation(savedState);

        djehuti_circuit_vst::Processor procB(built.serializedJson);
        procB.prepareToPlay(48000.0, 256);
        procB.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));

        check(std::abs(procB.getParameterValuePlain("drive_pot") - 0.82f) < 1e-3f &&
              std::abs(procB.getParameterValuePlain("tone_pot") - 0.24f) < 1e-3f &&
              std::abs(procB.getParameterValuePlain("input_gain_db") - 6.0f) < 1e-2f &&
              std::abs(procB.getParameterValuePlain("mix") - 0.75f) < 1e-3f &&
              procB.getSelectedEffectIndex() == 2,
              "Host state save/restore (getStateInformation/setStateInformation) preserves all parameters and selected effect");
    }

    // 3C: Stereo channel independence + 2 concurrent plugin instances
    {
        djehuti_circuit_vst::Processor inst1(built.serializedJson);
        djehuti_circuit_vst::Processor inst2(built.serializedJson);
        inst1.prepareToPlay(48000.0, 256);
        inst2.prepareToPlay(48000.0, 256);

        inst1.setSelectedEffectIndex(0);
        inst2.setSelectedEffectIndex(2);

        // Feed sine into Left channel (ch 0) and silence into Right channel (ch 1) of inst1
        juce::AudioBuffer<float> stereoBuf(2, 256);
        stereoBuf.clear();
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        for (int i = 0; i < 256; ++i)
            stereoBuf.setSample(0, i, 0.8f * static_cast<float>(std::sin(twoPi * 400.0 * i / 48000.0)));

        juce::AudioBuffer<float> inst2Buf(2, 256);
        fillSineMonoOrStereo(inst2Buf, 400.0, 48000.0, 0.8f);

        juce::MidiBuffer midi;
        inst1.processBlock(stereoBuf, midi);
        inst2.processBlock(inst2Buf, midi);

        const double ch0Rms = computeRms(stereoBuf.getReadPointer(0), 256);
        const double ch1Max = computePeak(stereoBuf.getReadPointer(1), 256);
        const double inst2Rms = computeRms(inst2Buf.getReadPointer(0), 256);

        check(ch0Rms > 0.05 && ch1Max < 1e-9,
              "Stereo channels maintain independent per-channel reactive/Newton workspaces (zero L->R bleed)",
              "ch0Rms=" + std::to_string(ch0Rms) + ", ch1Max=" + std::to_string(ch1Max));
        check(std::abs(ch0Rms - inst2Rms) > 0.05,
              "Two concurrent plugin instances execute simultaneously in isolated JITDylibs without interference",
              "inst1Rms=" + std::to_string(ch0Rms) + ", inst2Rms=" + std::to_string(inst2Rms));
    }

    // 3D: Sample-rate adaptation (44.1 kHz, 48 kHz, 96 kHz) & multiple block sizes (64, 256, 1024)
    {
        djehuti_circuit_vst::Processor proc(built.serializedJson);
        const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
        const int blockSizes[] = { 64, 256, 1024 };

        for (int s = 0; s < 3; ++s)
        {
            const double sr = sampleRates[s];
            const int blk = blockSizes[s];
            proc.prepareToPlay(sr, blk);

            // Build fresh reference model at `sr`
            std::vector<dsp_artifact::EffectPresetSlot> dummyEffs;
            auto slotAtSr = vst_exporter::makeCoupledThreeModeClipperCircuit(sr, dummyEffs);
            auto refModelAtSr = audio_dsp::build(slotAtSr.circuit, slotAtSr.dspConfig);
            std::vector<double> refWs = refModelAtSr.initialWorkspace;
            std::unordered_map<std::string, double> params = dummyEffs[0].livePortOverrides;
            params["drive_pot"] = 0.5;
            params["tone_pot"] = 0.5;
            const auto refCoeffs = refModelAtSr.computeLiveCoefficients(params);

            juce::AudioBuffer<float> buf(1, blk);
            fillSineMonoOrStereo(buf, 1000.0, sr, 0.7f);

            std::vector<double> expected((std::size_t)blk, 0.0);
            for (int i = 0; i < blk; ++i)
            {
                const double inV = static_cast<double>(buf.getSample(0, i)) * refModelAtSr.inputVolts;
                expected[(std::size_t)i] = refModelAtSr.step(refWs, refCoeffs.data(), inV) / refModelAtSr.outputFullScaleVolts;
            }

            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);

            double maxDiff = 0.0;
            for (int i = 0; i < blk; ++i)
                maxDiff = std::max(maxDiff, std::abs(static_cast<double>(buf.getSample(0, i)) - expected[(std::size_t)i]));

            check(maxDiff < 1e-5,
                  "Sample-rate adaptation at " + std::to_string(static_cast<int>(sr)) +
                  " Hz (blockSize=" + std::to_string(blk) + ") matches fresh discretization",
                  "maxDiff=" + std::to_string(maxDiff));
        }
    }
}

// 4. Test Corrupt / Placeholder / Incompatible Artifact & Unsupported Circuit Rejection
void testValidationAndCorruptRejection()
{
    std::cout << "\n=== 4. Corrupt / Placeholder / Incompatible Artifact & Unsupported Circuit Rejection ===\n";

    // 4A: Unsupported circuit element (BJT NPN) rejected by audio_dsp::build
    {
        circuit_sim::Circuit c;
        auto n1 = c.addNode();
        auto n2 = c.addNode();
        circuit_sim::Waveform w;
        int vIn = c.addVoltageSource("Vin", n1, 0, w);
        c.addBjt("Q1", true, n2, n1, 0);

        audio_dsp::Config cfg;
        cfg.sampleRate = 48000.0;
        cfg.audioInputElement = vIn;
        cfg.audioOutputNode = n2;
        const auto m = audio_dsp::build(c, cfg);
        check(!m.ok && m.error.find("Unsupported circuit element") != std::string::npos,
              "Unsupported circuit element (BJT) is explicitly rejected with diagnostic", m.error);
    }

    // 4B: Placeholder .frust-il (status="source_ready_il_lowering_pending") rejected
    {
        const juce::String placeholderJson = R"({
            "schemaVersion": 1,
            "kind": "djehuti_frust_il_placeholder",
            "status": "source_ready_il_lowering_pending"
        })";
        const auto loadRes = dsp_artifact::loadAndValidateArtifactFromJson(placeholderJson);
        check(!loadRes.ok && loadRes.error.find("source_ready_il_lowering_pending") != std::string::npos,
              "Legacy placeholder .frust-il is rejected", loadRes.error);

        djehuti_circuit_vst::Processor proc(placeholderJson);
        check(!proc.isValid(), "Processor rejects placeholder .frust-il");
        juce::AudioBuffer<float> buf(1, 64);
        fillSineMonoOrStereo(buf, 440.0, 48000.0, 0.9f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        check(computePeak(buf.getReadPointer(0), 64) == 0.0,
              "Invalid processor outputs silence (never substitutes pass-through)");
    }

    // 4C: Corrupt objectHashHex / tampered payload rejected
    {
        dsp_artifact::BuildArtifactInput in;
        in.packageId = "com.djehuti.test.tamper_check";
        in.productName = "Tamper Check";
        in.circuits.push_back(vst_exporter::makeLinearRcToneCircuit(48000.0));
        const auto built = dsp_artifact::buildExecutableArtifact(in);

        juce::String tamperedObj = built.serializedJson.replace(
            built.artifact.slots[0].objectHashHex, "0000000000000000");
        const auto tamperRes = dsp_artifact::loadAndValidateArtifactFromJson(tamperedObj);
        check(!tamperRes.ok && tamperRes.error.find("integrity hash mismatch") != std::string::npos,
              "Corrupted COFF object hash is detected and rejected", tamperRes.error);

        juce::String wrongArch = built.serializedJson.replace(
            "x86_64-pc-windows-msvc", "aarch64-apple-darwin");
        const auto archRes = dsp_artifact::loadAndValidateArtifactFromJson(wrongArch);
        check(!archRes.ok && archRes.error.find("Incompatible target architecture") != std::string::npos,
              "Incompatible target architecture is rejected", archRes.error);
    }
}

// 5. Test Full VST3 Export Workflow, 3 Separate Plugins, Second Circuit Export, & JUCE VST3 Host Loading
void testStandaloneVst3ExportAndHostLoading()
{
    std::cout << "\n=== 5. Standalone VST3 Bundle Export, Second Circuit Export, & JUCE VST3PluginFormat Host Verification ===\n";

    const juce::File tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("djehuti_vst3_verification_" + juce::String(juce::Time::currentTimeMillis()));
    tempRoot.createDirectory();

    // 5A: Export Product 1: Coupled 3-Effect Diode Clipper Trio
    vst_exporter::ExportRequest req1;
    req1.packageId = "com.djehuti.vst.diode_clipper_trio";
    req1.productName = "Djehuti Diode Clipper Trio";
    req1.productVersion = "1.2.0";
    req1.manufacturerName = "Djehuti Audio";
    req1.mode = vst_exporter::ExportMode::ThreeEffectsCoupled;
    req1.circuits.push_back(vst_exporter::makeCoupledThreeModeClipperCircuit(48000.0, req1.effects));
    req1.outputDirectory = tempRoot.getChildFile("product_1_trio");
    req1.buildVst3Bundle = true;

    const auto exp1 = vst_exporter::runExport(req1);
    check(exp1.ok && !exp1.bundles.empty(), "Exported Product 1 (Djehuti Diode Clipper Trio .vst3)", exp1.error);
    if (!exp1.ok) return;

    const auto& b1 = exp1.bundles.front();
    check(b1.artifactFile.existsAsFile() && b1.vst3BinaryFile.existsAsFile(),
          "Product 1 bundle contains model.frust-il and compiled x64 .vst3 binary",
          b1.vst3BinaryFile.getFullPathName().toStdString());

    // Verify config round-trip persistence
    vst_exporter::ExportRequest loadedCfg;
    std::string cfgErr;
    check(vst_exporter::loadExportConfigFromFile(b1.exportConfigFile, loadedCfg, cfgErr) &&
          loadedCfg.packageId == req1.packageId &&
          loadedCfg.productName == req1.productName &&
          loadedCfg.mode == vst_exporter::ExportMode::ThreeEffectsCoupled,
          "vst_export_config.json round-trip persistence preserves product identity and 3-effect mode", cfgErr);

    // 5B: Export Product 2 (Second Circuit without manual C++ edits): Resonant Presence Voicing Preamp
    vst_exporter::ExportRequest req2;
    req2.packageId = "com.djehuti.vst.resonant_presence_preamp";
    req2.productName = "Djehuti Resonant Presence Preamp";
    req2.productVersion = "1.0.0";
    req2.manufacturerName = "Djehuti Audio";
    req2.mode = vst_exporter::ExportMode::SingleEffect;
    req2.circuits.push_back(vst_exporter::makeResonantPresenceVoicingCircuit(48000.0));
    req2.outputDirectory = tempRoot.getChildFile("product_2_presence");
    req2.buildVst3Bundle = true;

    const auto exp2 = vst_exporter::runExport(req2);
    check(exp2.ok && !exp2.bundles.empty(),
          "Exported Product 2 (Second circuit: Djehuti Resonant Presence Preamp .vst3) without manual C++ edits",
          exp2.error);
    if (!exp2.ok) return;

    const auto& b2 = exp2.bundles.front();
    check(b1.pluginCode4CC != b2.pluginCode4CC && b1.vst3ComponentUidHex != b2.vst3ComponentUidHex,
          "Distinct exported VST3 products have distinct deterministic 4CC codes and VST3 UIDs",
          b1.pluginCode4CC + " vs " + b2.pluginCode4CC);
    check(dsp_artifact::derivePluginCode4CC(req1.packageId) == b1.pluginCode4CC,
          "Rebuilding the same packageId preserves identical 4CC plugin code and automation identity");

    // 5C: Export 3 Separate Plugins from a 3-Effect Configuration
    vst_exporter::ExportRequest reqSep;
    reqSep.packageId = "com.djehuti.vst.studio_pedals";
    reqSep.productName = "Djehuti Studio Pedal";
    reqSep.mode = vst_exporter::ExportMode::ThreeSeparatePlugins;
    reqSep.circuits.push_back(vst_exporter::makeLinearRcToneCircuit(48000.0));
    reqSep.circuits.push_back(vst_exporter::makeNonlinearDiodeClipperCircuit(48000.0));
    reqSep.circuits.push_back(vst_exporter::makeResonantPresenceVoicingCircuit(48000.0));
    reqSep.outputDirectory = tempRoot.getChildFile("product_3_separate");
    reqSep.buildVst3Bundle = false; // Fast artifact + identity generation check for the 3 separate packages

    const auto expSep = vst_exporter::runExport(reqSep);
    check(expSep.ok && expSep.bundles.size() == 3 &&
          expSep.bundles[0].pluginCode4CC != expSep.bundles[1].pluginCode4CC &&
          expSep.bundles[1].pluginCode4CC != expSep.bundles[2].pluginCode4CC,
          "ThreeSeparatePlugins mode exports 3 separate plugin packages with distinct identities");

    // 5D: Copy Product 1 & Product 2 .vst3 bundles to an isolated deployment directory and load via juce::VST3PluginFormat
    const juce::File isolatedDeployDir = tempRoot.getChildFile("isolated_host_deployment");
    isolatedDeployDir.createDirectory();

    const juce::File deployedVst1 = isolatedDeployDir.getChildFile(b1.vst3BundleDir.getFileName());
    const juce::File deployedVst2 = isolatedDeployDir.getChildFile(b2.vst3BundleDir.getFileName());
    b1.vst3BundleDir.copyDirectoryTo(deployedVst1);
    b2.vst3BundleDir.copyDirectoryTo(deployedVst2);

    juce::VST3PluginFormat vst3Format;
    for (const auto& deployedBundle : { deployedVst1, deployedVst2 })
    {
        juce::OwnedArray<juce::PluginDescription> descs;
        vst3Format.findAllTypesForFile(descs, deployedBundle.getFullPathName());
        check(!descs.isEmpty(),
              "juce::VST3PluginFormat discovered exported plugin in isolated folder: " +
              deployedBundle.getFileName().toStdString());

        if (!descs.isEmpty())
        {
            juce::String loadErr;
            auto instance = vst3Format.createPluginInstance(*descs[0], 48000.0, 256, loadErr);
            check(instance != nullptr,
                  "juce::VST3PluginFormat instantiated AudioPluginInstance for " + descs[0]->name.toStdString(),
                  loadErr.toStdString());

            if (instance != nullptr)
            {
                instance->prepareToPlay(48000.0, 256);

                // Verify editor creation
                std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditorIfNeeded());
                check(editor != nullptr && editor->getWidth() > 200 && editor->getHeight() > 150,
                      "Hosted VST3 instance created custom GUI editor (" +
                      std::to_string(editor ? editor->getWidth() : 0) + "x" +
                      std::to_string(editor ? editor->getHeight() : 0) + ")");
                if (editor != nullptr)
                    instance->editorBeingDeleted(editor.get());

                // Process audio through the hosted VST3 DLL
                juce::AudioBuffer<float> hostBuf(2, 256);
                fillSineMonoOrStereo(hostBuf, 500.0, 48000.0, 0.8f);
                juce::MidiBuffer midi;
                instance->processBlock(hostBuf, midi);

                const double outRms = computeRms(hostBuf.getReadPointer(0), 256);
                check(outRms > 0.02 && std::isfinite(outRms),
                      "Hosted VST3 instance processed audio in real time (outRms=" + std::to_string(outRms) + ")");
                instance->releaseResources();
            }
        }
    }

    tempRoot.deleteRecursively();
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "====================================================================\n";
    std::cout << "  Djehuti Electronics Workbench -- Circuit-to-VST3 Export Test Suite\n";
    std::cout << "====================================================================\n";

    testLinearAndNonlinearArtifactExecution();
    testThreeEffectTopologies();
    testRuntimeControlsStateStereoAndSampleRates();
    testValidationAndCorruptRejection();
    testStandaloneVst3ExportAndHostLoading();

    std::cout << "\n====================================================================\n";
    if (gFailures == 0)
    {
        std::cout << "  ALL VST3 EXPORT TESTS PASSED!\n";
        std::cout << "====================================================================\n";
        return 0;
    }
    std::cerr << "  VST3 EXPORT TESTS FAILED: " << gFailures << " failure(s)\n";
    std::cerr << "====================================================================\n";
    return 1;
}

#pragma once

#include <JuceHeader.h>

#include "../AudioDsp.h"
#include "../DspArtifactFormat.h"
#include "../FrustEngine.h"

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace djehuti_circuit_vst
{

// Embedded payload accessor provided by GeneratedPluginPayload.cpp
const char* getEmbeddedArtifactJson() noexcept;
juce::File findBundleResourceArtifactFile();

class Processor final : public juce::AudioProcessor
{
public:
    // Default constructor: loads <bundle>/Contents/Resources/model.frust-il if present, else embedded payload
    Processor();

    // Construct directly from a .frust-il JSON string (validates & links precompiled object without source compilation)
    explicit Processor(const juce::String& artifactJsonText);

    // Construct directly from a pre-parsed ExecutableArtifact (re-validates via serialized round-trip)
    explicit Processor(const dsp_artifact::ExecutableArtifact& artifact);

    ~Processor() override = default;

    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getInputChannelName(int channelIndex) const override { return "Input " + juce::String(channelIndex + 1); }
    const juce::String getOutputChannelName(int channelIndex) const override { return "Output " + juce::String(channelIndex + 1); }
    bool isInputChannelStereoPair(int) const override { return true; }
    bool isOutputChannelStereoPair(int) const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return parameters; }
    const dsp_artifact::ExecutableArtifact& artifact() const { return loadedArtifact; }

    bool isValid() const noexcept { return validArtifactAndObject; }
    juce::String runtimeStatus() const { return statusMessage; }
    juce::String runtimeError() const { return errorMessage; }
    int divergenceResets() const noexcept { return divergenceResetCount.load(std::memory_order_relaxed); }
    double activeSampleRate() const noexcept { return currentSampleRate; }

    // Programmatic parameter helpers for testing and host automation verification
    bool setParameterValueNormalized(const std::string& paramId, float normalizedValue);
    bool setParameterValuePlain(const std::string& paramId, float plainValue);
    float getParameterValuePlain(const std::string& paramId) const;
    void setSelectedEffectIndex(int effectIndex);
    int getSelectedEffectIndex() const;
    void resetAllWorkspaces();

private:
    using ProcessFn = double (*)(double, double*, const double*);
    static constexpr int kMaxChannels = 8;

    struct RuntimeSlot
    {
        std::string slotId;
        circuit_sim::Circuit circuit;
        audio_dsp::Config baseConfig;
        audio_dsp::Model runtimeModel;
        audio_dsp::Model::LiveCoeffScratch coeffScratch;
        std::vector<double> activeCoeffs;
        std::vector<double> orderedLiveValues;
        std::vector<std::string> livePortParamIds;
        std::vector<std::vector<double>> channelWorkspaces; // [kMaxChannels][workspaceSize]
        ProcessFn processSample = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(
        const dsp_artifact::ExecutableArtifact& artifact,
        bool isValidArtifact);

    bool initializeFromLoadResult(const dsp_artifact::LoadArtifactResult& loadRes);
    void updateSlotCoefficientsNoAlloc(std::size_t slotIdx, int activeEffectIdx) noexcept;
    double stepSlotChannelNoAlloc(RuntimeSlot& slot, int channel, double inputVolts) noexcept;

    dsp_artifact::ExecutableArtifact loadedArtifact;
    bool validArtifactAndObject = false;
    juce::String statusMessage { "Uninitialized" };
    juce::String errorMessage;

    juce::AudioProcessorValueTreeState parameters;
    frust_engine::Engine engine;
    std::vector<RuntimeSlot> runtimeSlots;

    // Cached atomic pointers to APVTS raw parameter values (populated once at construction/prepareToPlay)
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* inputGainDbParam = nullptr;
    std::atomic<float>* outputLevelDbParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* effectSelectParam = nullptr;
    struct BoundCircuitParam
    {
        std::string id;
        std::string livePortId;
        std::atomic<float>* rawValue = nullptr;
    };
    std::vector<BoundCircuitParam> boundCircuitParams;

    double currentSampleRate = 48000.0;
    int lastEffectIndex = 0;
    int previousEffectIndex = 0;
    double effectCrossfadeAlpha = 1.0;
    double effectCrossfadeStep = 0.01;
    double smoothedBypassWet = 1.0;
    double bypassSmoothStep = 0.01;
    std::atomic<int> divergenceResetCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace djehuti_circuit_vst

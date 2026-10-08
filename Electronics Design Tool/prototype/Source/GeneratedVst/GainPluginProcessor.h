#pragma once

#include <JuceHeader.h>

#include "../FrustEngine.h"

#include <array>
#include <memory>
#include <vector>

namespace djehuti_generated_gain
{
class Processor final : public juce::AudioProcessor
{
public:
    Processor();
    ~Processor() override = default;

    const juce::String getName() const override { return "Djehuti Generated Gain"; }
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return parameters; }
    juce::String runtimeStatus() const { return status; }

private:
    using ProcessFn = double (*)(double, double*, const double*);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    bool compileModel();

    juce::AudioProcessorValueTreeState parameters;
    frust_engine::Engine engine;
    ProcessFn processSample = nullptr;
    std::vector<double> workspace { 0.0 };
    std::array<double, 1> coeffs { 1.0 };
    juce::String status { "Model not compiled." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

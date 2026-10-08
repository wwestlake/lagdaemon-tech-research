#include "../../Source/GeneratedVst/GainPluginProcessor.h"

#include <iostream>

namespace
{
int failures = 0;

void check(bool ok, const char* message)
{
    if (!ok)
    {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
    else
    {
        std::cout << "PASS: " << message << "\n";
    }
}

void checkNear(float actual, float expected, float tolerance, const char* message)
{
    check(std::abs(actual - expected) <= tolerance, message);
    if (std::abs(actual - expected) > tolerance)
        std::cerr << "      expected " << expected << ", got " << actual << "\n";
}
}

int main()
{
    djehuti_generated_gain::Processor processor;
    processor.setRateAndBufferSizeDetails(48000.0, 16);
    processor.prepareToPlay(48000.0, 16);

    check(processor.getParameters().size() == 1, "generated plugin exposes one model-specific DAW parameter");
    check(processor.getParameters()[0]->getName(64) == "Gain", "DAW parameter is named Gain");
    check(processor.runtimeStatus().contains("Loaded embedded model"), "embedded Frust model loaded");

    if (auto* gain = dynamic_cast<juce::AudioParameterFloat*>(processor.getParameters()[0]))
        *gain = 0.25f;
    else
        check(false, "Gain parameter has float type");

    juce::AudioBuffer<float> buffer(2, 8);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, i == 0 ? 1.0f : -0.5f);

    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    checkNear(buffer.getSample(0, 0), 0.25f, 1.0e-6f, "Frust IL/source DSP applies gain parameter to left channel");
    checkNear(buffer.getSample(1, 1), -0.125f, 1.0e-6f, "Frust IL/source DSP applies gain parameter to right channel");

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    check(state.getSize() > 0, "plugin saves package identity and parameter state");

    djehuti_generated_gain::Processor restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());
    restored.prepareToPlay(48000.0, 16);
    juce::AudioBuffer<float> restoredBuffer(1, 1);
    restoredBuffer.setSample(0, 0, 1.0f);
    restored.processBlock(restoredBuffer, midi);
    checkNear(restoredBuffer.getSample(0, 0), 0.25f, 1.0e-6f, "saved state restores Gain value");

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}

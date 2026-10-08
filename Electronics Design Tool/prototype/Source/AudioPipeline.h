#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

class AudioPipeline : public juce::AudioIODeviceCallback
{
public:
    AudioPipeline();
    ~AudioPipeline() override;

    // Call this to open the settings window
    void showAudioSettings(juce::Component* parentComponent);

    // Call this to set the Frust DSP processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numSamples)>;
    void setProcessCallback(ProcessCallback callback);

    // juce::AudioIODeviceCallback overrides
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    void setRouting(const juce::String& inType, const juce::String& inFile, const juce::String& outType, const juce::String& outFile);

private:
    juce::AudioDeviceManager deviceManager;
    juce::CriticalSection callbackLock;
    ProcessCallback processCallback;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::int64 currentReadPosition = 0;
    bool useHardwareIn = true;
    bool useHardwareOut = true;
    std::vector<float> fileInputBuffer;
    std::vector<float> tempOutBuffer;
};


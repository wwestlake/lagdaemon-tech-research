#include "AudioPipeline.h"

AudioPipeline::AudioPipeline()
{
    deviceManager.initialiseWithDefaultDevices(1, 2); // 1 input, 2 outputs default
    deviceManager.addAudioCallback(this);
    formatManager.registerBasicFormats();
}

AudioPipeline::~AudioPipeline()
{
    deviceManager.removeAudioCallback(this);
}

void AudioPipeline::showAudioSettings(juce::Component* parentComponent)
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio Settings";
    auto* selector = new juce::AudioDeviceSelectorComponent(deviceManager, 1, 2, 1, 2, false, false, true, false);
    selector->setSize(500, 400);
    o.content.setOwned(selector);
    o.componentToCentreAround = parentComponent;
    o.launchAsync();
}

void AudioPipeline::setRouting(const juce::String& inType, const juce::String& inFile, const juce::String& outType, const juce::String& outFile)
{
    useHardwareIn = (inType != "File");
    useHardwareOut = (outType != "File");
    
    reader.reset();
    writer.reset();
    currentReadPosition = 0;
    
    if (!useHardwareIn && inFile.isNotEmpty()) {
        auto file = juce::File(inFile);
        if (file.existsAsFile()) {
            reader.reset(formatManager.createReaderFor(file));
        }
    }
    
    if (!useHardwareOut && outFile.isNotEmpty()) {
        auto file = juce::File(outFile);
        if (auto* wavFormat = formatManager.findFormatForFileExtension("wav")) {
            file.deleteFile();
            writer.reset(wavFormat->createWriterFor(new juce::FileOutputStream(file),
                                                    deviceManager.getAudioDeviceSetup().sampleRate,
                                                    1, 16, {}, 0));
        }
    }
}

void AudioPipeline::setProcessCallback(ProcessCallback callback)
{
    processCallback = std::move(callback);
}

void AudioPipeline::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);
    
    // Clear the output buffers first
    for (int i = 0; i < numOutputChannels; ++i)
    {
        if (outputChannelData[i] != nullptr)
        {
            juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
        }
    }

    if (processCallback)
    {
        std::vector<float> fileInputBuffer;
        const float* input = nullptr;
        
        if (!useHardwareIn && reader != nullptr) {
            fileInputBuffer.resize(numSamples, 0.0f);
            float* dests[] = { fileInputBuffer.data() };
            juce::AudioBuffer<float> buf(dests, 1, numSamples);
            reader->read(&buf, 0, numSamples, currentReadPosition, true, false);
            currentReadPosition += numSamples;
            input = fileInputBuffer.data();
        } else {
            input = (numInputChannels > 0 && inputChannelData[0] != nullptr) ? inputChannelData[0] : nullptr;
        }

        std::vector<float> tempOutBuffer;
        float* output = nullptr;
        
        if (!useHardwareOut && writer != nullptr) {
            tempOutBuffer.resize(numSamples, 0.0f);
            output = tempOutBuffer.data();
        } else {
            output = (numOutputChannels > 0 && outputChannelData[0] != nullptr) ? outputChannelData[0] : nullptr;
        }
        
        if (output != nullptr || (!useHardwareOut && writer != nullptr))
        {
            if (output == nullptr) {
                tempOutBuffer.resize(numSamples, 0.0f);
                output = tempOutBuffer.data();
            }

            if (input != nullptr)
            {
                processCallback(input, output, numSamples);
            }
            else
            {
                std::vector<float> silence(numSamples, 0.0f);
                processCallback(silence.data(), output, numSamples);
            }
            
            if (!useHardwareOut && writer != nullptr) {
                const float* sources[] = { output };
                writer->writeFromFloatArrays(sources, 1, numSamples);
                // Silence hardware outputs
                for (int i = 0; i < numOutputChannels; ++i) {
                    if (outputChannelData[i] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
                }
            } else {
                for (int i = 1; i < numOutputChannels; ++i)
                {
                    if (outputChannelData[i] != nullptr)
                    {
                        std::copy(output, output + numSamples, outputChannelData[i]);
                    }
                }
            }
        }
    }
    else
    {
        // Pass through
        if (numInputChannels > 0 && numOutputChannels > 0)
        {
            const float* in = inputChannelData[0];
            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                if (outputChannelData[ch] != nullptr && in != nullptr)
                    std::copy(in, in + numSamples, outputChannelData[ch]);
            }
        }
    }
}

void AudioPipeline::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    juce::ignoreUnused(device);
}

void AudioPipeline::audioDeviceStopped()
{
}


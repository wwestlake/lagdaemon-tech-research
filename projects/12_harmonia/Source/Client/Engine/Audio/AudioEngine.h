#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AdditiveVoice.h"

namespace Harmonia {
class AudioEngine : public juce::AudioSource {
public:
    AudioEngine();
    ~AudioEngine() override;
    
    bool initialise();
    void shutdown();
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    
    void noteOn(int midiNote, float velocity, int channel);
    void noteOff(int midiNote, int channel);
    
    void setTimbre(AdditiveVoice::Timbre t);
    juce::AudioDeviceManager& deviceManager();
    
private:
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    juce::Synthesiser synth_;
    juce::MidiMessageCollector midiCollector_;
};
}

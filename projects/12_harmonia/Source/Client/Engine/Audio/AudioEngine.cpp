#include "AudioEngine.h"

namespace Harmonia {
struct HarmoniaSound : public juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

AudioEngine::AudioEngine() {
    for (int i = 0; i < 16; ++i) synth_.addVoice(new AdditiveVoice());
    synth_.addSound(new HarmoniaSound());
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::initialise() {
    deviceManager_.initialiseWithDefaultDevices(0, 2);
    player_.setSource(this);
    deviceManager_.addAudioCallback(&player_);
    return true;
}

void AudioEngine::shutdown() {
    deviceManager_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    synth_.setCurrentPlaybackSampleRate(sampleRate);
    midiCollector_.reset(sampleRate);
}

void AudioEngine::releaseResources() {}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    bufferToFill.clearActiveBufferRegion();
    juce::MidiBuffer incomingMidi;
    midiCollector_.removeNextBlockOfMessages(incomingMidi, bufferToFill.numSamples);
    synth_.renderNextBlock(*bufferToFill.buffer, incomingMidi, bufferToFill.startSample, bufferToFill.numSamples);
}

void AudioEngine::noteOn(int midiNote, float velocity, int channel) {
    synth_.noteOn(channel, midiNote, velocity);
}

void AudioEngine::noteOff(int midiNote, int channel) {
    synth_.noteOff(channel, midiNote, 0.0f, true);
}

void AudioEngine::setTimbre(AdditiveVoice::Timbre t) {
    AdditiveVoice::setGlobalTimbre(t);
}

juce::AudioDeviceManager& AudioEngine::deviceManager() { return deviceManager_; }
}

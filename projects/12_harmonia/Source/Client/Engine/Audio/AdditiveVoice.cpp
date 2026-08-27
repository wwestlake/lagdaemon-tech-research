#include "AdditiveVoice.h"

namespace Harmonia {
float AdditiveVoice::harmonicAmps_[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

void AdditiveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) {
    freq_ = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    level_ = velocity;
    envelope_ = 1.0f; // Simplified
    releasing_ = false;
}

void AdditiveVoice::stopNote(float, bool) {
    releasing_ = true;
    clearCurrentNote();
}

void AdditiveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (envelope_ <= 0.0f) {
        clearCurrentNote();
        return;
    }
    
    // Additive synthesis simplified rendering
    for (int i = 0; i < numSamples; ++i) {
        float sample = 0;
        for (int h = 0; h < 8; ++h) {
            sample += std::sin(phase_[h]) * harmonicAmps_[h];
            phase_[h] += (freq_ * (h+1)) * 2.0 * juce::MathConstants<double>::pi / getSampleRate();
        }
        
        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
            outputBuffer.addSample(ch, startSample + i, sample * level_ * envelope_ * 0.1f);
        }
    }
}

void AdditiveVoice::setGlobalTimbre(Timbre t) {
    // Modify harmonicAmps_ based on preset
}
}

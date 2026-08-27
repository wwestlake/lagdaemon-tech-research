#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace Harmonia {
class AdditiveVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPos) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    
    enum class Timbre { Sine, Bell, Organ, String, Glass };
    static void setGlobalTimbre(Timbre t);
    
private:
    double phase_[8] = {};
    double freq_ = 440.0;
    float level_ = 0.f;
    float envelope_ = 0.f;
    bool releasing_ = false;
    
    float attack_ = 0.01f;
    float decay_ = 0.1f;
    float sustain_ = 0.7f;
    float release_ = 0.3f;
    
    static float harmonicAmps_[8];
};
}

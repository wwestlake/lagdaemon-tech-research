#include "AdditiveVoice.h"
#include <cmath>

namespace Harmonia {

// Default: pure sine. Presets fill in the harmonics.
float AdditiveVoice::harmonicAmps_[8] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };

// ── Timbre presets ────────────────────────────────────────────────────────────
// h1   h2    h3    h4    h5    h6    h7    h8
static const float kSine  [8] = { 1.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f,  0.f };
static const float kBell  [8] = { 1.f,  0.f,  0.7f, 0.f,  0.5f, 0.f,  0.3f, 0.f };
static const float kOrgan [8] = { 1.f,  0.8f, 0.6f, 0.4f, 0.3f, 0.2f, 0.f,  0.f };
static const float kString[8] = { 1.f,  0.9f, 0.5f, 0.3f, 0.2f, 0.1f, 0.1f, 0.05f };
static const float kGlass [8] = { 1.f,  0.f,  0.3f, 0.f,  0.f,  0.1f, 0.f,  0.f };

void AdditiveVoice::setGlobalTimbre(Timbre t) {
    const float* src = kSine;
    switch (t) {
        case Timbre::Bell:   src = kBell;   break;
        case Timbre::Organ:  src = kOrgan;  break;
        case Timbre::String: src = kString; break;
        case Timbre::Glass:  src = kGlass;  break;
        default: break;
    }
    for (int i = 0; i < 8; ++i) harmonicAmps_[i] = src[i];
}

// ── ADSR presets per timbre ───────────────────────────────────────────────────
void AdditiveVoice::applyTimbreEnvelope() {
    // Defaults
    attack_  = 0.01f;
    decay_   = 0.15f;
    sustain_ = 0.7f;
    release_ = 0.4f;

    // Adjust per predominant harmonic texture
    if (harmonicAmps_[2] > 0.5f && harmonicAmps_[1] < 0.1f) {
        // Bell-like — fast attack, long decay/release, no sustain
        attack_  = 0.005f;
        decay_   = 0.5f;
        sustain_ = 0.0f;
        release_ = 1.5f;
    } else if (harmonicAmps_[1] > 0.7f) {
        // Organ-like — fast attack, full sustain
        attack_  = 0.01f;
        decay_   = 0.05f;
        sustain_ = 0.9f;
        release_ = 0.08f;
    }
}

void AdditiveVoice::startNote(int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int /*pitchWheelPos*/) {
    freq_      = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    level_     = velocity;
    envelope_  = 0.f;        // start at zero, ADSR will ramp up
    releasing_ = false;
    phase_pos_ = 0.f;        // reset ADSR phase position

    for (int h = 0; h < 8; ++h) phase_[h] = 0.0;

    applyTimbreEnvelope();
}

void AdditiveVoice::stopNote(float /*velocity*/, bool allowTailOff) {
    if (allowTailOff && envelope_ > 0.f) {
        releasing_ = true;   // let ADSR release run
    } else {
        envelope_  = 0.f;
        releasing_ = false;
        clearCurrentNote();
    }
}

void AdditiveVoice::renderNextBlock(juce::AudioBuffer<float>& output,
                                   int startSample, int numSamples) {
    if (envelope_ <= 0.f && releasing_) {
        clearCurrentNote();
        return;
    }

    const double sr     = getSampleRate();
    const double twoPi  = juce::MathConstants<double>::twoPi;
    const float  invSr  = 1.f / (float)sr;

    // ADSR time constants in samples
    const float attackSamples  = attack_  * (float)sr;
    const float decaySamples   = decay_   * (float)sr;
    const float releaseSamples = release_ * (float)sr;

    for (int i = 0; i < numSamples; ++i) {
        // ── Envelope ──────────────────────────────────────────────────────
        if (!releasing_) {
            phase_pos_ += 1.f;
            if (phase_pos_ < attackSamples) {
                // Attack
                envelope_ = phase_pos_ / attackSamples;
            } else if (phase_pos_ < attackSamples + decaySamples) {
                // Decay → sustain
                float t   = (phase_pos_ - attackSamples) / decaySamples;
                envelope_ = 1.f - t * (1.f - sustain_);
            } else {
                // Sustain
                envelope_ = sustain_;
            }
        } else {
            // Release
            envelope_ = juce::jmax(0.f, envelope_ - (1.f / releaseSamples));
            if (envelope_ <= 0.f) {
                clearCurrentNote();
                return;
            }
        }

        // ── Additive synthesis ────────────────────────────────────────────
        float sample = 0.f;
        for (int h = 0; h < 8; ++h) {
            if (harmonicAmps_[h] < 0.001f) continue;
            sample += (float)std::sin(phase_[h]) * harmonicAmps_[h];
            phase_[h] += freq_ * (double)(h + 1) * twoPi * invSr;
            // Wrap to avoid float precision drift
            if (phase_[h] > twoPi) phase_[h] -= twoPi;
        }

        const float out = sample * level_ * envelope_ * 0.12f; // 0.12 keeps 16 voices safe
        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.addSample(ch, startSample + i, out);
    }
}

} // namespace Harmonia

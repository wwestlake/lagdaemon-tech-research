#include "CircleOfFifths.h"
#include "MusicTheory.h"
#include <juce_core/juce_core.h>

namespace Harmonia {

juce::Colour CircleOfFifths::colourForPitchClass(int pitchClass) {
    // Circle of fifths position: C=0, G=1, D=2, A=3, E=4, B=5,
    // F#=6, C#=7, Ab=8, Eb=9, Bb=10, F=11
    int pos = MusicTheory::circleOfFifthsPosition(pitchClass);
    // Hue varies 0.0 to 1.0 around the circle
    float hue = static_cast<float>(pos) / 12.0f;
    return juce::Colour::fromHSV(hue, 0.85f, 0.9f, 1.0f);
}

juce::Colour CircleOfFifths::colourForMidiNote(int midiNote) {
    return colourForPitchClass(MusicTheory::pitchClass(midiNote));
}

juce::Colour CircleOfFifths::blendColours(int pcA, int pcB, float t) {
    auto cA = colourForPitchClass(pcA);
    auto cB = colourForPitchClass(pcB);
    return cA.interpolatedWith(cB, t);
}

juce::Colour CircleOfFifths::tensionColour(float dissonance) {
    // 0 = warm amber (consonant) to 1 = icy blue (dissonant)
    // Amber hue ~ 0.1, Blue hue ~ 0.6
    float hue = juce::jmap(dissonance, 0.0f, 1.0f, 0.1f, 0.6f);
    return juce::Colour::fromHSV(hue, 0.7f, 0.9f, 1.0f);
}

juce::Colour CircleOfFifths::withActivation(juce::Colour base, float activation) {
    float h, s, v, a;
    base.getHSB(h, s, v);
    a = base.getFloatAlpha();
    
    // boost brightness and lower saturation for high activation
    float newV = juce::jmap(activation, 0.3f, 1.0f);
    float newS = juce::jmap(activation, s, 0.4f); // more white when highly active
    return juce::Colour::fromHSV(h, newS, newV, a);
}

} // namespace Harmonia

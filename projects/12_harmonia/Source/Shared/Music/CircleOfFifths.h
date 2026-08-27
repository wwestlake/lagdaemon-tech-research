#pragma once

#include <juce_graphics/juce_graphics.h>

namespace Harmonia {

class CircleOfFifths {
public:
    // Returns a JUCE Colour for a pitch class (0-11)
    static juce::Colour colourForPitchClass(int pitchClass);
    static juce::Colour colourForMidiNote(int midiNote);
    
    // Returns a colour blended between two pitch classes
    static juce::Colour blendColours(int pcA, int pcB, float t);
    
    // Interval tension colour: 0=warm amber (consonant) to 1=icy blue (dissonant)  
    static juce::Colour tensionColour(float dissonance);
    
    // Returns a brightened/dimmed version for activation strength
    static juce::Colour withActivation(juce::Colour base, float activation); // 0-1
};

} // namespace Harmonia

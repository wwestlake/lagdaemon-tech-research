#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace Harmonia {

class MusicTheory {
public:
    // Note names
    static juce::String noteName(int midiNote);           // "C4", "F#3" etc
    static juce::String noteClass(int midiNote);          // "C", "F#" etc  
    static int          pitchClass(int midiNote);         // 0-11
    static int          octave(int midiNote);             // -1 to 9
    static int          midiNote(int pitchClass, int oct);// build from parts
    
    // Intervals
    static juce::String intervalName(int semitones);      // "P5", "m3", "TT" etc
    static float        consonanceScore(int semitones);   // 0.0=dissonant 1.0=unison
    
    // Chords — detect what chord a set of notes forms
    static juce::String chordSymbol(const std::vector<int>& midiNotes); // "Cmaj7", "Dm"
    static juce::String romanNumeral(int rootPitchClass, int keyPitchClass, const std::vector<int>& notes);
    
    // Scales
    static std::vector<int> scaleIntervals(const juce::String& scaleName); // "major","minor","dorian" etc
    static bool             noteInScale(int pitchClass, int keyPC, const juce::String& scaleName);
    
    // Circle of fifths position: C=0, G=1, D=2 ... (clockwise)
    static int              circleOfFifthsPosition(int pitchClass);
};

} // namespace Harmonia

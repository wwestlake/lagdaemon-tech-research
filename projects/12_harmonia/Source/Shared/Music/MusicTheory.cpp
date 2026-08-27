#include "MusicTheory.h"
#include <algorithm>

namespace Harmonia {

static const char* kNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"
};

static const char* kIntervalNames[] = {
    "P1", "m2", "M2", "m3", "M3", "P4", "TT", "P5", "m6", "M6", "m7", "M7", "P8"
};

juce::String MusicTheory::noteName(int midiNote) {
    if (midiNote < 0 || midiNote > 127) return "???";
    return noteClass(midiNote) + juce::String(octave(midiNote));
}

juce::String MusicTheory::noteClass(int midiNote) {
    if (midiNote < 0 || midiNote > 127) return "???";
    return kNoteNames[pitchClass(midiNote)];
}

int MusicTheory::pitchClass(int midiNote) {
    return midiNote % 12;
}

int MusicTheory::octave(int midiNote) {
    return (midiNote / 12) - 1;
}

int MusicTheory::midiNote(int pitchClass, int oct) {
    return (oct + 1) * 12 + (pitchClass % 12);
}

juce::String MusicTheory::intervalName(int semitones) {
    int st = std::abs(semitones) % 12;
    if (std::abs(semitones) == 12) return "P8";
    return kIntervalNames[st];
}

float MusicTheory::consonanceScore(int semitones) {
    // Very naive scoring for intervals (0.0 to 1.0)
    int st = std::abs(semitones) % 12;
    switch (st) {
        case 0: return 1.0f;   // Unison
        case 7: return 0.9f;   // Perfect 5th
        case 5: return 0.85f;  // Perfect 4th
        case 4: return 0.8f;   // Major 3rd
        case 3: return 0.75f;  // Minor 3rd
        case 9: return 0.7f;   // Major 6th
        case 8: return 0.65f;  // Minor 6th
        case 2: return 0.3f;   // Major 2nd
        case 10: return 0.3f;  // Minor 7th
        case 11: return 0.1f;  // Major 7th
        case 1: return 0.1f;   // Minor 2nd
        case 6: return 0.0f;   // Tritone
        default: return 0.0f;
    }
}

juce::String MusicTheory::chordSymbol(const std::vector<int>& midiNotes) {
    if (midiNotes.empty()) return "";
    if (midiNotes.size() == 1) return noteClass(midiNotes[0]);

    std::vector<int> pcs;
    for (int n : midiNotes) {
        int pc = pitchClass(n);
        if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end()) {
            pcs.push_back(pc);
        }
    }
    
    // Sort to normalize (this is naive, usually need to find the root properly)
    std::sort(pcs.begin(), pcs.end());

    // Simple brute-force to find a root that makes a known chord structure
    for (int root : pcs) {
        bool hasM3 = false;
        bool hasm3 = false;
        bool hasP5 = false;
        bool hasdim5 = false;
        bool hasm7 = false;
        bool hasM7 = false;

        for (int pc : pcs) {
            int interval = (pc - root + 12) % 12;
            if (interval == 3) hasm3 = true;
            if (interval == 4) hasM3 = true;
            if (interval == 6) hasdim5 = true;
            if (interval == 7) hasP5 = true;
            if (interval == 10) hasm7 = true;
            if (interval == 11) hasM7 = true;
        }

        juce::String rootName = kNoteNames[root];

        if (hasM3 && hasP5 && hasM7) return rootName + "maj7";
        if (hasM3 && hasP5 && hasm7) return rootName + "7";
        if (hasm3 && hasP5 && hasm7) return rootName + "m7";
        if (hasm3 && hasdim5 && hasm7) return rootName + "m7b5";
        if (hasm3 && hasdim5 && !hasP5) return rootName + "dim";
        if (hasM3 && hasP5) return rootName;
        if (hasm3 && hasP5) return rootName + "m";
    }

    return noteClass(midiNotes[0]) + " (cluster)";
}

juce::String MusicTheory::romanNumeral(int rootPitchClass, int keyPitchClass, const std::vector<int>& notes) {
    // Very rudimentary roman numeral calculation
    int interval = (rootPitchClass - keyPitchClass + 12) % 12;
    juce::String numeral = "";
    switch (interval) {
        case 0: numeral = "I"; break;
        case 2: numeral = "II"; break;
        case 4: numeral = "III"; break;
        case 5: numeral = "IV"; break;
        case 7: numeral = "V"; break;
        case 9: numeral = "VI"; break;
        case 11: numeral = "VII"; break;
        default: numeral = "?"; break;
    }
    // Check if minor
    bool minor = false;
    for (int n : notes) {
        if ((pitchClass(n) - rootPitchClass + 12) % 12 == 3) {
            minor = true;
            break;
        }
    }
    return minor ? numeral.toLowerCase() : numeral;
}

std::vector<int> MusicTheory::scaleIntervals(const juce::String& scaleName) {
    if (scaleName == "major") return {0, 2, 4, 5, 7, 9, 11};
    if (scaleName == "minor") return {0, 2, 3, 5, 7, 8, 10};
    if (scaleName == "dorian") return {0, 2, 3, 5, 7, 9, 10};
    if (scaleName == "phrygian") return {0, 1, 3, 5, 7, 8, 10};
    if (scaleName == "lydian") return {0, 2, 4, 6, 7, 9, 11};
    if (scaleName == "mixolydian") return {0, 2, 4, 5, 7, 9, 10};
    if (scaleName == "locrian") return {0, 1, 3, 5, 6, 8, 10};
    // Default to major
    return {0, 2, 4, 5, 7, 9, 11};
}

bool MusicTheory::noteInScale(int pitchClass, int keyPC, const juce::String& scaleName) {
    auto intervals = scaleIntervals(scaleName);
    int rel = (pitchClass - keyPC + 12) % 12;
    return std::find(intervals.begin(), intervals.end(), rel) != intervals.end();
}

int MusicTheory::circleOfFifthsPosition(int pitchClass) {
    // C=0, G=1, D=2, A=3, E=4, B=5, F#=6, C#/Db=7, Ab=8, Eb=9, Bb=10, F=11
    // (pitchClass * 7) % 12 works for perfect fifths!
    return (pitchClass * 7) % 12;
}

} // namespace Harmonia

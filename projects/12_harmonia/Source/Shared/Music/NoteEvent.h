#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cstdint>

namespace Harmonia {

struct NoteEvent {
    int          midiNote;     // 0-127
    float        velocity;     // 0.0-1.0  
    int          channel;      // 1-16
    double       durationMs;   // for note-off scheduling, 0=manual off
    uint32_t     playerID;     // who triggered it (0=local)
    juce::Colour visualColour; // from CircleOfFifths
};

} // namespace Harmonia

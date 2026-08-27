#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>
#include <memory>
#include "VoxelGrid.h"

namespace Harmonia {

struct PlayerState {
    uint32_t     playerID;
    juce::String playerName;
    float        x, y, z, yaw;
    float        colorHue;        // 0-1, maps to pitch class colour
    bool         connected;
};

struct WorldState {
    std::shared_ptr<VoxelGrid>       livingGrid;   // World 1
    std::vector<int>                 chordStack;   // World 3 — current shared chord (midi notes)
    int                              currentInterval[2]; // World 2 — root, semitones
    std::map<uint32_t, PlayerState>  players;
    uint32_t                         generation;   // CA generation counter
    juce::CriticalSection            lock;
};

} // namespace Harmonia

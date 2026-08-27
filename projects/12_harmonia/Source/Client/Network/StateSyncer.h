#pragma once
#include <juce_core/juce_core.h>
#include "Shared/World/WorldState.h"

namespace Harmonia {
class StateSyncer {
public:
    StateSyncer(WorldState* ws);
    void applyFullSync(const juce::MemoryBlock& payload);
    void applyVoxelDelta(const juce::MemoryBlock& payload);
private:
    WorldState* worldState_;
};
}

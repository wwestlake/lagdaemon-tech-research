#include "StateSyncer.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {
StateSyncer::StateSyncer(WorldState* ws) : worldState_(ws) {}

void StateSyncer::applyFullSync(const juce::MemoryBlock& payload) {
    // Deserialize voxel grid
}

void StateSyncer::applyVoxelDelta(const juce::MemoryBlock& payload) {
    // Apply delta
}
}

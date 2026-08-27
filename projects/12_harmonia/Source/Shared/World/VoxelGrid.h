#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace Harmonia {

class VoxelGrid {
public:
    struct Voxel {
        float    state;        // 0.0=dead, 0.0-1.0=activation strength
        uint8_t  pitchClass;   // 0-11, derived from X position
        uint8_t  octave;       // derived from Y position
        uint8_t  beatSlot;     // derived from Z position
        bool     justFired;    // true for one tick after note triggered
    };

    VoxelGrid(int width, int height, int depth);
    
    int width() const; int height() const; int depth() const;
    
    Voxel&       at(int x, int y, int z);
    const Voxel& at(int x, int y, int z) const;
    bool         inBounds(int x, int y, int z) const;
    
    void  seedRandom(float density);   // random sparse seed
    void  clear();
    void  setVoxel(int x, int y, int z, float state); // thread-safe
    
    // Serialization for VoxelFullSync / VoxelDelta
    juce::MemoryBlock serialiseFull() const;
    void              deserialise(const juce::MemoryBlock& data);
    
    // Delta: returns list of changed voxels since last call
    struct Delta { uint8_t x, y, z; float state; };
    std::vector<Delta> collectDelta();
    void               clearDirty();
    
    // Pitch/time mapping: x→pitchClass(0-11), y→octave(0-7), z→beatSlot
    static int xToPitchClass(int x, int gridWidth);  // x mod 12
    static int yToOctave(int y, int gridHeight);     // maps y to octave 2-8
    static int zToBeatSlot(int z, int gridDepth);    // 0-based beat position
    
private:
    int w_, h_, d_;
    std::vector<Voxel>  cells_;
    std::vector<bool>   dirty_;
    mutable juce::CriticalSection lock_;
    
    int index(int x, int y, int z) const { return x + w_ * (y + h_ * z); }
};

} // namespace Harmonia

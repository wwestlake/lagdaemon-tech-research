#include "VoxelGrid.h"
#include <random>
#include <stdexcept>
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {

VoxelGrid::VoxelGrid(int width, int height, int depth)
    : w_(width), h_(height), d_(depth) {
    cells_.resize(w_ * h_ * d_);
    dirty_.resize(w_ * h_ * d_, false);
    clear();
}

int VoxelGrid::width() const { return w_; }
int VoxelGrid::height() const { return h_; }
int VoxelGrid::depth() const { return d_; }

VoxelGrid::Voxel& VoxelGrid::at(int x, int y, int z) {
    if (!inBounds(x, y, z)) throw std::out_of_range("VoxelGrid bounds");
    return cells_[index(x, y, z)];
}

const VoxelGrid::Voxel& VoxelGrid::at(int x, int y, int z) const {
    if (!inBounds(x, y, z)) throw std::out_of_range("VoxelGrid bounds");
    return cells_[index(x, y, z)];
}

bool VoxelGrid::inBounds(int x, int y, int z) const {
    return x >= 0 && x < w_ && y >= 0 && y < h_ && z >= 0 && z < d_;
}

void VoxelGrid::seedRandom(float density) {
    juce::ScopedLock sl(lock_);
    std::mt19937 gen(static_cast<uint32_t>(juce::Time::currentTimeMillis()));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    for (int z = 0; z < d_; ++z) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                float state = (dist(gen) < density) ? 1.0f : 0.0f;
                setVoxel(x, y, z, state);
            }
        }
    }
}

void VoxelGrid::clear() {
    juce::ScopedLock sl(lock_);
    for (int z = 0; z < d_; ++z) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                int idx = index(x, y, z);
                cells_[idx].state = 0.0f;
                cells_[idx].pitchClass = static_cast<uint8_t>(xToPitchClass(x, w_));
                cells_[idx].octave = static_cast<uint8_t>(yToOctave(y, h_));
                cells_[idx].beatSlot = static_cast<uint8_t>(zToBeatSlot(z, d_));
                cells_[idx].justFired = false;
                dirty_[idx] = true;
            }
        }
    }
}

void VoxelGrid::setVoxel(int x, int y, int z, float state) {
    juce::ScopedLock sl(lock_);
    if (!inBounds(x, y, z)) return;
    int idx = index(x, y, z);
    if (std::abs(cells_[idx].state - state) > 0.001f) {
        cells_[idx].state = state;
        dirty_[idx] = true;
    }
}

juce::MemoryBlock VoxelGrid::serialiseFull() const {
    juce::ScopedLock sl(lock_);
    
    juce::MemoryBlock mb;
    mb.ensureSize(3 + w_ * h_ * d_ * sizeof(float));
    uint8_t* ptr = static_cast<uint8_t*>(mb.getData());
    ptr[0] = static_cast<uint8_t>(w_);
    ptr[1] = static_cast<uint8_t>(h_);
    ptr[2] = static_cast<uint8_t>(d_);
    
    float* fptr = reinterpret_cast<float*>(ptr + 3);
    for (int idx = 0; idx < w_ * h_ * d_; ++idx) {
        uint32_t bits;
        float val = cells_[idx].state;
        std::memcpy(&bits, &val, sizeof(float));
        bits = juce::ByteOrder::swapIfBigEndian(bits);
        std::memcpy(&fptr[idx], &bits, sizeof(float));
    }
    return mb;
}

void VoxelGrid::deserialise(const juce::MemoryBlock& data) {
    juce::ScopedLock sl(lock_);
    Net::HarpReader reader(data);
    
    int newW = reader.readU8();
    int newH = reader.readU8();
    int newD = reader.readU8();
    
    if (newW != w_ || newH != h_ || newD != d_) {
        w_ = newW; h_ = newH; d_ = newD;
        cells_.resize(w_ * h_ * d_);
        dirty_.resize(w_ * h_ * d_, true);
    }
    
    for (int z = 0; z < d_; ++z) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                int idx = index(x, y, z);
                cells_[idx].state = reader.readF32();
                cells_[idx].pitchClass = static_cast<uint8_t>(xToPitchClass(x, w_));
                cells_[idx].octave = static_cast<uint8_t>(yToOctave(y, h_));
                cells_[idx].beatSlot = static_cast<uint8_t>(zToBeatSlot(z, d_));
                cells_[idx].justFired = false;
                dirty_[idx] = true;
            }
        }
    }
}

std::vector<VoxelGrid::Delta> VoxelGrid::collectDelta() {
    juce::ScopedLock sl(lock_);
    std::vector<Delta> deltas;
    for (int z = 0; z < d_; ++z) {
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                int idx = index(x, y, z);
                if (dirty_[idx]) {
                    deltas.push_back({ static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(z), cells_[idx].state });
                }
            }
        }
    }
    return deltas;
}

void VoxelGrid::clearDirty() {
    juce::ScopedLock sl(lock_);
    std::fill(dirty_.begin(), dirty_.end(), false);
}

int VoxelGrid::xToPitchClass(int x, int gridWidth) {
    return x % 12; // Maps x strictly to 0-11 pitch class
}

int VoxelGrid::yToOctave(int y, int gridHeight) {
    int octaves = 7;
    int mapped = (y * octaves) / std::max(1, gridHeight);
    return mapped + 2; // e.g. octaves 2-8
}

int VoxelGrid::zToBeatSlot(int z, int gridDepth) {
    return z;
}

} // namespace Harmonia

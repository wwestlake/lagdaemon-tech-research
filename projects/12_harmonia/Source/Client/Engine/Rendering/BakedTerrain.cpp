#include "BakedTerrain.h"
#include "TerrainHeight.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace Harmonia {

BakedTerrain& BakedTerrain::get() {
    static BakedTerrain instance;
    return instance;
}

void BakedTerrain::bakeAndSave(const std::string& path, float minX, float minZ, float maxX, float maxZ, float step) {
    minX_ = minX;
    minZ_ = minZ;
    maxX_ = maxX;
    maxZ_ = maxZ;
    step_ = step;

    width_ = static_cast<int>(std::ceil((maxX - minX) / step)) + 1;
    height_ = static_cast<int>(std::ceil((maxZ - minZ) / step)) + 1;

    heights_.resize(width_ * height_);

    std::cout << "Baking terrain (" << width_ << "x" << height_ << ")...\n";

    for (int z = 0; z < height_; ++z) {
        for (int x = 0; x < width_; ++x) {
            float worldX = minX_ + x * step_;
            float worldZ = minZ_ + z * step_;
            heights_[z * width_ + x] = TerrainHeight::heightAt(worldX, worldZ);
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&minX_), sizeof(float));
        out.write(reinterpret_cast<const char*>(&minZ_), sizeof(float));
        out.write(reinterpret_cast<const char*>(&maxX_), sizeof(float));
        out.write(reinterpret_cast<const char*>(&maxZ_), sizeof(float));
        out.write(reinterpret_cast<const char*>(&step_), sizeof(float));
        out.write(reinterpret_cast<const char*>(&width_), sizeof(int));
        out.write(reinterpret_cast<const char*>(&height_), sizeof(int));
        out.write(reinterpret_cast<const char*>(heights_.data()), heights_.size() * sizeof(float));
        out.close();
        std::cout << "Saved baked terrain to " << path << "\n";
    } else {
        std::cerr << "Failed to save baked terrain to " << path << "\n";
    }
}

bool BakedTerrain::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    in.read(reinterpret_cast<char*>(&minX_), sizeof(float));
    in.read(reinterpret_cast<char*>(&minZ_), sizeof(float));
    in.read(reinterpret_cast<char*>(&maxX_), sizeof(float));
    in.read(reinterpret_cast<char*>(&maxZ_), sizeof(float));
    in.read(reinterpret_cast<char*>(&step_), sizeof(float));
    in.read(reinterpret_cast<char*>(&width_), sizeof(int));
    in.read(reinterpret_cast<char*>(&height_), sizeof(int));

    heights_.resize(width_ * height_);
    in.read(reinterpret_cast<char*>(heights_.data()), heights_.size() * sizeof(float));
    
    in.close();
    std::cout << "Loaded baked terrain from " << path << " (" << width_ << "x" << height_ << ")\n";
    return true;
}

float BakedTerrain::heightAt(float x, float z) const {
    if (heights_.empty()) return -50.0f; // Default void height if not loaded

    // Transform world coords to grid coords
    float gx = (x - minX_) / step_;
    float gz = (z - minZ_) / step_;

    // Clamp to bounds (avoid reading out of array)
    if (gx < 0.0f) gx = 0.0f;
    if (gz < 0.0f) gz = 0.0f;
    if (gx >= width_ - 1.0f) gx = width_ - 1.001f;
    if (gz >= height_ - 1.0f) gz = height_ - 1.001f;

    int ix = static_cast<int>(gx);
    int iz = static_cast<int>(gz);
    float fx = gx - ix;
    float fz = gz - iz;

    // Bilinear interpolation
    float h00 = heights_[iz * width_ + ix];
    float h10 = heights_[iz * width_ + (ix + 1)];
    float h01 = heights_[(iz + 1) * width_ + ix];
    float h11 = heights_[(iz + 1) * width_ + (ix + 1)];

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
}

} // namespace Harmonia

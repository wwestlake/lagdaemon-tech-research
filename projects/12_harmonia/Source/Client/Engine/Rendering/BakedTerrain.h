#pragma once

#include <vector>
#include <string>

namespace Harmonia {

class BakedTerrain {
public:
    static BakedTerrain& get();

    void bakeAndSave(const std::string& path, float minX, float minZ, float maxX, float maxZ, float step);
    bool load(const std::string& path);

    // Get interpolated height at world coordinates
    float heightAt(float x, float z) const;

private:
    BakedTerrain() = default;

    std::vector<float> heights_;
    int width_ = 0;
    int height_ = 0;
    float minX_ = 0.0f;
    float minZ_ = 0.0f;
    float maxX_ = 0.0f;
    float maxZ_ = 0.0f;
    float step_ = 1.0f;
};

} // namespace Harmonia

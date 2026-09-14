#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include "TerrainChunk.h"

namespace Harmonia {

// Chunked, streamed, LOD'd terrain - replaces the old single 1000-unit
// monolithic GroundPlane mesh. Chunks near the camera mesh at high
// resolution; far chunks mesh coarser; chunks well outside range are
// freed entirely. Colour is fully procedural (slope-driven grass/rock
// blend, independently-noised dirt/mud patches) - no texture images yet,
// so there's no UV-stretching concern to solve with triplanar mapping;
// that becomes relevant once real texture assets replace this.
class Terrain {
public:
    Terrain();
    ~Terrain();

    // Streams chunks in/out and rebuilds any whose LOD should change,
    // based on the camera's current position. Call once per frame,
    // before render().
    void update(juce::OpenGLContext& ctx, const glm::vec3& camPos);

    void render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& camPos);

private:
    void initShader(juce::OpenGLContext& ctx);
    int desiredLod(float distanceToCamera) const;

    struct ChunkKeyHash {
        size_t operator()(const std::pair<int, int>& k) const {
            return std::hash<long long>()(((long long)k.first << 32) ^ (unsigned int)k.second);
        }
    };

    std::unordered_map<std::pair<int, int>, TerrainChunk, ChunkKeyHash> chunks_;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
    
    juce::OpenGLTexture texGrassColor_;
    juce::OpenGLTexture texGrassNormal_;
    juce::OpenGLTexture texRockColor_;
    juce::OpenGLTexture texRockNormal_;
    
    bool shaderInitialised_ = false;
};

}

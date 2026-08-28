#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>
#include "Shared/World/VoxelGrid.h"
#include "Shared/Music/CircleOfFifths.h"

namespace Harmonia {

// ─────────────────────────────────────────────────────────────────────────────
// VoxelRenderer
// Renders the living voxel grid using hardware instancing.
// Each voxel maps to one instance with: position, colour, glow strength.
// Only active voxels (state > threshold) are submitted.
// ─────────────────────────────────────────────────────────────────────────────
class VoxelRenderer {
public:
    // Per-vertex data for unit cube — public so file-scope array initializer can use it
    struct Vertex {
        float x, y, z;
        float nx, ny, nz;
    };

    VoxelRenderer() = default;
    ~VoxelRenderer() { shutdown(); }

    void initialise(juce::OpenGLContext& ctx);
    void shutdown();

    // Upload latest voxel data from the grid (call from GL thread)
    void update(const VoxelGrid& grid);

    // Draw with the voxel shader already bound; VP = projection * view
    void draw(juce::OpenGLShaderProgram& shader,
              const glm::mat4& vp,
              const glm::vec3& camPos,
              float time,
              float voxelSize = 0.9f);

    int activeVoxelCount() const { return instanceCount_; }

private:
    // Per-instance data uploaded to GPU each frame
    struct Instance {
        float ox, oy, oz;
        float r, g, b, a;
        float glow;
    };

    GLuint vao_        = 0;
    GLuint cubeVBO_    = 0;
    GLuint cubeEBO_    = 0;
    GLuint instanceVBO_= 0;

    std::vector<Instance> instances_;
    int instanceCount_ = 0;
    juce::OpenGLContext* ctx_ = nullptr;

    static constexpr float kMinState = 0.05f;
};

} // namespace Harmonia

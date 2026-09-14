#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>

namespace Harmonia {

// One chunk of terrain, meshed at a given resolution (LOD). Owns nothing
// but its own GL buffers - the shared shader/uniforms live on Terrain,
// the manager, since every chunk uses the exact same shader.
class TerrainChunk {
public:
    TerrainChunk();
    ~TerrainChunk();

    // Builds (or rebuilds, e.g. on an LOD change) this chunk's mesh -
    // verticesPerSide x verticesPerSide grid covering [originX,
    // originX+size] x [originZ, originZ+size], plus a downward "skirt"
    // around the perimeter so neighbouring chunks at DIFFERENT LODs
    // (and therefore not sharing edge vertices) don't show a visible
    // crack between them.
    void build(juce::OpenGLContext& ctx, float originX, float originZ, float size, int verticesPerSide);
    void render(juce::OpenGLContext& ctx);
    void release(juce::OpenGLContext& ctx);

    int lod = -1; // -1 = not yet built; set by Terrain to track what's loaded

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    int indexCount_ = 0;
};

}

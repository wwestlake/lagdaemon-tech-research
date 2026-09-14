#include "TerrainChunk.h"
#include "BakedTerrain.h"
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

namespace {
constexpr float kSkirtDepth = 12.0f; // deep enough to hide seams even at steep LOD/height mismatches
}

TerrainChunk::TerrainChunk() {}
TerrainChunk::~TerrainChunk() {}

void TerrainChunk::build(juce::OpenGLContext& ctx, float originX, float originZ, float size, int verticesPerSide) {
    auto& ext = ctx.extensions;

    const int res = verticesPerSide;
    const float step = size / (float)(res - 1);

    // 6 floats/vertex: pos.xyz, normal.xyz - no UV, colour is computed
    // purely from world position/normal/slope in the fragment shader.
    std::vector<float> verts;
    verts.reserve((size_t)(res * res + res * 4) * 6);

    auto heightAndNormal = [&](float x, float z, float eps) {
        auto& bt = BakedTerrain::get();
        float y = bt.heightAt(x, z);
        float hL = bt.heightAt(x - eps, z);
        float hR = bt.heightAt(x + eps, z);
        float hD = bt.heightAt(x, z - eps);
        float hU = bt.heightAt(x, z + eps);
        glm::vec3 n = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
        return std::pair<float, glm::vec3>(y, n);
    };

    // Main grid
    for (int gz = 0; gz < res; ++gz) {
        for (int gx = 0; gx < res; ++gx) {
            float x = originX + gx * step;
            float z = originZ + gz * step;
            auto [y, n] = heightAndNormal(x, z, step * 0.5f);
            verts.insert(verts.end(), { x, y, z, n.x, n.y, n.z });
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve((size_t)(res - 1) * (res - 1) * 6);
    for (int gz = 0; gz < res - 1; ++gz) {
        for (int gx = 0; gx < res - 1; ++gx) {
            unsigned int i0 = gz * res + gx;
            unsigned int i1 = i0 + 1;
            unsigned int i2 = i0 + res;
            unsigned int i3 = i2 + 1;
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }

    // Skirt: for each of the 4 border edges, duplicate that edge's
    // vertices lowered by kSkirtDepth, and stitch a downward-facing wall
    // between the real edge and its lowered copy. Whatever the neighbour
    // chunk's LOD is, this wall covers any gap - much simpler than
    // stitching shared edge vertices between differently-resolved chunks.
    auto addSkirtEdge = [&](int startIdx, int count, int strideToNext) {
        unsigned int base = (unsigned int)(verts.size() / 6);
        for (int i = 0; i < count; ++i) {
            unsigned int srcIdx = (unsigned int)(startIdx + i * strideToNext);
            float x = verts[srcIdx * 6 + 0];
            float y = verts[srcIdx * 6 + 1];
            float z = verts[srcIdx * 6 + 2];
            float nx = verts[srcIdx * 6 + 3];
            float ny = verts[srcIdx * 6 + 4];
            float nz = verts[srcIdx * 6 + 5];
            verts.insert(verts.end(), { x, y - kSkirtDepth, z, nx, ny, nz });
            if (i + 1 < count) {
                unsigned int top0 = srcIdx;
                unsigned int top1 = (unsigned int)(startIdx + (i + 1) * strideToNext);
                unsigned int bot0 = base + i;
                unsigned int bot1 = base + i + 1;
                indices.insert(indices.end(), { top0, bot0, top1, top1, bot0, bot1 });
            }
        }
    };

    addSkirtEdge(0, res, 1);                       // north edge (gz = 0)
    addSkirtEdge((res - 1) * res, res, 1);         // south edge (gz = res-1)
    addSkirtEdge(0, res, res);                     // west edge (gx = 0)
    addSkirtEdge(res - 1, res, res);                // east edge (gx = res-1)

    indexCount_ = (int)indices.size();

    if (!vao_) {
        ext.glGenVertexArrays(1, &vao_);
        ext.glGenBuffers(1, &vbo_);
        ext.glGenBuffers(1, &ebo_);
    }

    ext.glBindVertexArray(vao_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

    const int stride = 6 * sizeof(float);
    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    ext.glEnableVertexAttribArray(1);
    ext.glBindVertexArray(0);
}

void TerrainChunk::render(juce::OpenGLContext& ctx) {
    if (!vao_ || indexCount_ == 0) return;
    auto& ext = ctx.extensions;
    ext.glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, (void*)0);
    ext.glBindVertexArray(0);
}

void TerrainChunk::release(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;
    if (vao_) { ext.glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { ext.glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { ext.glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    indexCount_ = 0;
    lod = -1;
}

}

#include "Terrain.h"
#include <juce_graphics/juce_graphics.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

namespace {
constexpr float kChunkSize = 100.0f;

// LOD verts-per-side, near to far. Powers-of-two-plus-one, matching the
// same convention PhysicsWorld's Jolt heightfield already uses.
constexpr int kLodRes[3] = { 33, 17, 9 };
constexpr float kLodDistance[3] = { 180.0f, 400.0f, 1.0e9f }; // last is "anything beyond"

constexpr float kStreamRadius = 650.0f; // build/keep chunks within this
constexpr float kUnloadRadius = 750.0f; // free chunks beyond this (hysteresis gap avoids load/unload thrashing at the edge)
}

Terrain::Terrain() {}
Terrain::~Terrain() {}

int Terrain::desiredLod(float distanceToCamera) const {
    for (int i = 0; i < 3; ++i)
        if (distanceToCamera < kLodDistance[i]) return i;
    return 2;
}

void Terrain::update(juce::OpenGLContext& ctx, const glm::vec3& camPos) {
    int centreCx = (int)std::floor(camPos.x / kChunkSize);
    int centreCz = (int)std::floor(camPos.z / kChunkSize);
    int reach = (int)std::ceil(kStreamRadius / kChunkSize);

    // Ensure every chunk within range exists and is at the right LOD.
    for (int cz = centreCz - reach; cz <= centreCz + reach; ++cz) {
        for (int cx = centreCx - reach; cx <= centreCx + reach; ++cx) {
            float originX = cx * kChunkSize;
            float originZ = cz * kChunkSize;
            float centreX = originX + kChunkSize * 0.5f;
            float centreZ = originZ + kChunkSize * 0.5f;
            float dist = std::sqrt((centreX - camPos.x) * (centreX - camPos.x) +
                                    (centreZ - camPos.z) * (centreZ - camPos.z));
            if (dist > kStreamRadius) continue;

            int wantLod = desiredLod(dist);
            auto key = std::make_pair(cx, cz);
            auto& chunk = chunks_[key]; // default-constructs (lod == -1) if new

            if (chunk.lod != wantLod) {
                chunk.build(ctx, originX, originZ, kChunkSize, kLodRes[wantLod]);
                chunk.lod = wantLod;
            }
        }
    }

    // Free anything that's drifted out of range.
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        float centreX = it->first.first * kChunkSize + kChunkSize * 0.5f;
        float centreZ = it->first.second * kChunkSize + kChunkSize * 0.5f;
        float dist = std::sqrt((centreX - camPos.x) * (centreX - camPos.x) +
                                (centreZ - camPos.z) * (centreZ - camPos.z));
        if (dist > kUnloadRadius) {
            it->second.release(ctx);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

void Terrain::initShader(juce::OpenGLContext& ctx) {
    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        uniform mat4 view;
        uniform mat4 proj;
        out vec3 worldPos;
        out vec3 normal;
        void main() {
            worldPos = aPos;
            normal = aNormal;
            gl_Position = proj * view * vec4(aPos, 1.0);
        }
    )";

    // Layered, fully procedural terrain colour - no texture images yet:
    //   - slope (from the normal) blends grass -> rock, flat = grass,
    //     steep = rock, with a smooth transition band
    //   - an independently-seeded noise field creates irregular dirt/mud
    //     patches, gated to the grass side only (no mud on a cliff face)
    //   - a second, coarser noise breaks up flat rock into two tones so
    //     steep areas read as real geology, not one flat grey
    const char* fShader = R"(
        #version 330 core
        in vec3 worldPos;
        in vec3 normal;
        out vec4 FragColor;
        
        uniform vec3 sunDir;
        uniform vec3 sunColor;
        uniform vec3 camPos;
        
        uniform sampler2D texGrassColor;
        // uniform sampler2D texGrassNormal;
        uniform sampler2D texRockColor;
        // uniform sampler2D texRockNormal;

        void main() {
            vec3 N = normalize(normal);
            // Ultra-cheap single texture sample for performance testing
            // Scaled to 0.5 as requested by user
            vec4 col = texture(texGrassColor, worldPos.xz * 0.5);
            
            // Lighting
            float diffuse = 0.8 + 0.4 * max(dot(N, normalize(sunDir)), 0.0);
            col.rgb *= diffuse;
            vec3 softTint = mix(sunColor, vec3(1.0), 0.6);
            vec3 finalRgb = mix(col.rgb, col.rgb * softTint, 0.25);
            
            float dist = length(worldPos.xz - camPos.xz);
            float alpha = smoothstep(700.0, 500.0, dist);

            FragColor = vec4(finalRgb, alpha);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();
    
    auto& ext = ctx.extensions;
    // Load textures
    auto loadTex = [&ext](const juce::String& path, juce::OpenGLTexture& tex) {
        juce::File f(path);
        if (f.existsAsFile()) {
            juce::Image img = juce::ImageFileFormat::loadFrom(f);
            if (img.isValid()) {
                tex.loadImage(img);
                tex.bind();
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#ifdef _WIN32
                typedef void (WINAPI * PFNGLGENERATEMIPMAPPROC) (GLenum target);
                PFNGLGENERATEMIPMAPPROC glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
                if (glGenerateMipmap) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                }
#endif
                tex.unbind();
            }
        }
    };
    loadTex("D:/CreationSuite-Workspaces/Assets/Landscape/Textures/forrest_ground_01/forrest_ground_01_diff_2k.png", texGrassColor_);
    loadTex("D:/CreationSuite-Workspaces/Assets/Landscape/Textures/forrest_ground_01/forrest_ground_01_nor_gl_2k.png", texGrassNormal_);
    loadTex("D:/CreationSuite-Workspaces/Assets/Landscape/Textures/rocky_terrain/rocky_terrain_diff_2k.png", texRockColor_);
    loadTex("D:/CreationSuite-Workspaces/Assets/Landscape/Textures/rocky_terrain/rocky_terrain_nor_gl_2k.png", texRockNormal_);

    shaderInitialised_ = true;
}

void Terrain::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                      const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& camPos) {
    if (!shaderInitialised_) initShader(ctx);
    if (!shader_) return;

    auto& ext = ctx.extensions;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    shader_->use();
    GLint progId = (GLint)shader_->getProgramID();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunDir"), sunDir.x, sunDir.y, sunDir.z);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunColor"), sunColor.x, sunColor.y, sunColor.z);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "camPos"), camPos.x, camPos.y, camPos.z);
    
    // Bind Grass Texture
    ext.glActiveTexture(GL_TEXTURE0);
    texGrassColor_.bind();
    ext.glUniform1i(ext.glGetUniformLocation(progId, "texGrassColor"), 0);

    // Bind Rock Texture
    ext.glActiveTexture(GL_TEXTURE1);
    texRockColor_.bind();
    ext.glUniform1i(ext.glGetUniformLocation(progId, "texRockColor"), 1);

    for (auto& [key, chunk] : chunks_) {
        chunk.render(ctx);
    }

    texGrassColor_.unbind();
    texRockColor_.unbind();
    glDisable(GL_BLEND);
}

}

#include "GroundPlane.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

namespace {

// Self-contained hash-based value noise (no external noise library) - a
// classic bilinear-interpolated lattice noise, smootherstepped so it has
// no visible grid artifacts. Deterministic: same (x, z) always gives the
// same height, on any machine, with no seed state to sync over the network.
float hash01(int x, int y) {
    int n = x * 374761393 + y * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    n = n ^ (n >> 16);
    return (n & 0x7fffffff) / float(0x7fffffff);
}

float smootherstep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float valueNoise(float x, float y) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float sx = smootherstep(x - (float)x0), sy = smootherstep(y - (float)y0);
    float n00 = hash01(x0, y0), n10 = hash01(x1, y0);
    float n01 = hash01(x0, y1), n11 = hash01(x1, y1);
    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy; // [0, 1]
}

// Fractal Brownian motion: several octaves of the noise above, each
// higher-frequency layer contributing less - gives natural-looking,
// non-repetitive variation instead of one smooth sine-like bump.
float fbm(float x, float y, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += (valueNoise(x * freq, y * freq) * 2.0f - 1.0f) * amp; // [-1, 1] contribution
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f; // ~[-1, 1]
}

float smoothstepEdge(float e0, float e1, float x) {
    // Manual clamp, not std::min/max - windows.h's own min/max macros
    // (pulled in above for GL_) mangle std::min/std::max call syntax.
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kHalfExtent = 500.0f; // matches the old flat quad's footprint
constexpr int   kGridRes    = 160;    // vertices per side
constexpr float kMaxHillHeight = 18.0f; // real peak height where hills occur - still gentle relative to a 1000-unit-wide world

} // namespace

// Large-scale "roughness mask" (low frequency) decides WHERE hills happen;
// smaller-scale fbm decides their shape. A continuous multiplicative bias
// (e.g. pow()) was tried first and measured (via a standalone numeric
// check, not just eyeballing screenshots) to compound with fbm's own
// sub-theoretical-max practical range - the tallest point on the WHOLE map
// came out under 1 unit, invisible everywhere. smoothstep here instead
// creates genuine near-0 (flat) or near-1 (full hill amplitude) regions
// with a smooth transition between them, so hilly patches actually reach
// close to kMaxHillHeight rather than being uniformly crushed down.
float GroundPlane::heightAt(float worldX, float worldZ) {
    const float maskFreq = 1.0f / 150.0f;
    float maskRaw = fbm(worldX * maskFreq, worldZ * maskFreq, 3, 2.0f, 0.5f);
    maskRaw = (maskRaw + 1.0f) * 0.5f; // [0, 1]
    float mask = smoothstepEdge(0.35f, 0.65f, maskRaw);

    const float hillFreq = 1.0f / 90.0f;
    float hills = fbm(worldX * hillFreq, worldZ * hillFreq, 4, 2.0f, 0.5f); // [-1, 1]

    return hills * kMaxHillHeight * mask;
}

GroundPlane::GroundPlane() {}

GroundPlane::~GroundPlane() {}

namespace {

void buildTerrainMesh(std::vector<float>& verts, std::vector<unsigned int>& indices) {
    // 8 floats/vertex: pos.xyz, uv.xy, normal.xyz
    verts.reserve((size_t)kGridRes * kGridRes * 8);

    const float step = (kHalfExtent * 2.0f) / (float)(kGridRes - 1);
    const float uvScale = 0.2f; // keeps the checker squares a sensible world size

    for (int gz = 0; gz < kGridRes; ++gz) {
        for (int gx = 0; gx < kGridRes; ++gx) {
            float x = -kHalfExtent + gx * step;
            float z = -kHalfExtent + gz * step;
            float y = GroundPlane::heightAt(x, z);

            // Central-difference normal, sampling the same height function
            // the mesh itself uses - stays consistent even where the mask
            // makes the surface locally steep.
            float eps = step * 0.5f;
            float hL = GroundPlane::heightAt(x - eps, z);
            float hR = GroundPlane::heightAt(x + eps, z);
            float hD = GroundPlane::heightAt(x, z - eps);
            float hU = GroundPlane::heightAt(x, z + eps);
            glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));

            verts.insert(verts.end(), {
                x, y, z,
                x * uvScale, z * uvScale,
                normal.x, normal.y, normal.z
            });
        }
    }

    indices.reserve((size_t)(kGridRes - 1) * (kGridRes - 1) * 6);
    for (int gz = 0; gz < kGridRes - 1; ++gz) {
        for (int gx = 0; gx < kGridRes - 1; ++gx) {
            unsigned int i0 = gz * kGridRes + gx;
            unsigned int i1 = i0 + 1;
            unsigned int i2 = i0 + kGridRes;
            unsigned int i3 = i2 + 1;
            indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }
}

} // namespace

void GroundPlane::init(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;

    std::vector<float> verts;
    std::vector<unsigned int> indices;
    buildTerrainMesh(verts, indices);
    indexCount_ = (int)indices.size();

    ext.glGenVertexArrays(1, &vao_);
    ext.glBindVertexArray(vao_);

    ext.glGenBuffers(1, &vbo_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    ext.glGenBuffers(1, &ebo_);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    const int stride = 8 * sizeof(float);
    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    ext.glEnableVertexAttribArray(1);
    ext.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    ext.glEnableVertexAttribArray(2);

    ext.glBindVertexArray(0);

    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUV;
        layout (location = 2) in vec3 aNormal;
        uniform mat4 view;
        uniform mat4 proj;
        out vec2 uv;
        out vec3 worldPos;
        out vec3 normal;
        void main() {
            uv = aUV;
            worldPos = aPos;
            normal = aNormal;
            gl_Position = proj * view * vec4(aPos, 1.0);
        }
    )";

    const char* fShader = R"(
        #version 330 core
        in vec2 uv;
        in vec3 worldPos;
        in vec3 normal;
        out vec4 FragColor;
        uniform vec3 sunDir;
        uniform vec3 sunColor;

        void main() {
            // Checkerboard pattern
            vec2 c = floor(uv);
            float checker = mod(c.x + c.y, 2.0);
            vec3 col = mix(vec3(0.1, 0.1, 0.15), vec3(0.15, 0.15, 0.2), checker);

            // Real directional shading from the actual sun, not a fixed
            // hardcoded light - so hills read as hills AND the ground
            // picks up a subtle wash of the sun's current seasonal colour.
            float diffuse = 0.6 + 0.4 * max(dot(normalize(normal), normalize(sunDir)), 0.0);
            col *= diffuse;
            // Subtle seasonal wash: desaturate the sun colour toward white
            // before tinting, so a fully-saturated hue (e.g. pure red)
            // doesn't crush the ground's green/blue channels to zero.
            vec3 softTint = mix(sunColor, vec3(1.0), 0.6);
            col = mix(col, col * softTint, 0.25);

            // Fog fade-out based on distance
            float dist = length(worldPos.xz);
            float alpha = smoothstep(300.0, 100.0, dist);

            FragColor = vec4(col, alpha);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();
}

void GroundPlane::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                          const glm::vec3& sunDir, const glm::vec3& sunColor) {
    if (!vao_) init(ctx);
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

    ext.glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, (void*)0);
    ext.glBindVertexArray(0);

    glDisable(GL_BLEND);
}

}

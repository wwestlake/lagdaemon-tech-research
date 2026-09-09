#include "Sun.h"
#include "Shared/Music/CircleOfFifths.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

namespace {
constexpr float kDayDuration    = 60.0f;                 // seconds per in-game "day"
constexpr float kSeasonDuration = kDayDuration * 12.0f;   // 12 days per full colour cycle - one day per wheel colour
constexpr float kSunDistance    = 2000.0f;
constexpr float kSunSize        = 120.0f;

// A slight axial tilt (not a straight vertical spin axis) so the day arc
// looks like a real sky rotating around one axis, not a sun bobbing on a
// fixed vertical hinge. StarField shares this exact same rotation.
const glm::vec3 kSkyAxis = glm::normalize(glm::vec3(1.0f, 0.0f, 0.35f));
}

Sun::Sun() {}
Sun::~Sun() {}

void Sun::update(float dt) {
    dayPhase_ += dt / kDayDuration;
    dayPhase_ -= std::floor(dayPhase_);

    seasonPhase_ += dt / kSeasonDuration;
    seasonPhase_ -= std::floor(seasonPhase_);
}

glm::mat4 Sun::skyRotation() const {
    float angle = dayPhase_ * 2.0f * 3.14159265f;
    return glm::rotate(glm::mat4(1.0f), angle, kSkyAxis);
}

glm::vec3 Sun::direction() const {
    // A fixed "sunrise" position rotated once per day around the shared
    // sky axis - a real single-axis rotation (a great-circle arc through
    // overhead and back down below the horizon on the far side), not a
    // hand-tuned wobble. StarField rotates by this exact same matrix, so
    // the sun and stars move together as one sky, not independently.
    glm::vec3 base(0.0f, -0.3f, 1.0f);
    return glm::normalize(glm::vec3(skyRotation() * glm::vec4(base, 0.0f)));
}

glm::vec3 Sun::color() const {
    // Smoothly rotates through all 12 circle-of-fifths hues over the
    // season cycle via the SAME colour wheel note/chord visualization
    // already uses - blended continuously (blendColours), so the shift
    // reads as subtle drift, not a strobe between hard-edged colours.
    float pos = seasonPhase_ * 12.0f;
    int pcA = ((int)std::floor(pos)) % 12;
    int pcB = (pcA + 1) % 12;
    float t = pos - std::floor(pos);
    juce::Colour c = CircleOfFifths::blendColours(pcA, pcB, t);
    return glm::vec3(c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue());
}

void Sun::init(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;

    // A single quad in local (-1..1) space - billboarded toward the
    // camera entirely in the vertex shader using the view matrix's own
    // right/up basis vectors, so no per-frame CPU vertex work is needed.
    std::vector<float> verts = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    ext.glGenVertexArrays(1, &vao_);
    ext.glBindVertexArray(vao_);
    ext.glGenBuffers(1, &vbo_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    ext.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glBindVertexArray(0);

    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec2 aCorner;
        uniform mat4 view;
        uniform mat4 proj;
        uniform vec3 center;
        uniform float size;
        out vec2 localPos;
        void main() {
            vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
            vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);
            vec3 worldPos = center + (right * aCorner.x + up * aCorner.y) * size;
            localPos = aCorner;
            gl_Position = proj * view * vec4(worldPos, 1.0);
        }
    )";

    const char* fShader = R"(
        #version 330 core
        in vec2 localPos;
        uniform vec3 sunColor;
        out vec4 FragColor;
        void main() {
            float d = length(localPos);
            // Bright solid core, soft glow falling off to nothing past the
            // disc edge - reads as an actual sun, not a coloured square.
            float core = 1.0 - smoothstep(0.35, 0.55, d);
            float glow = 1.0 - smoothstep(0.0, 1.0, d);
            float alpha = max(core, glow * 0.5);
            if (alpha < 0.01) discard;
            vec3 col = sunColor * mix(0.8, 1.6, core);
            FragColor = vec4(col, alpha);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();
}

void Sun::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx) {
    if (!vao_) init(ctx);
    if (!shader_) return;

    auto& ext = ctx.extensions;
    glm::vec3 center = direction() * kSunDistance;
    glm::vec3 col = color();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_->use();
    GLint progId = (GLint)shader_->getProgramID();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    ext.glUniform3f(ext.glGetUniformLocation(progId, "center"), center.x, center.y, center.z);
    ext.glUniform1f(ext.glGetUniformLocation(progId, "size"), kSunSize);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunColor"), col.x, col.y, col.z);

    ext.glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ext.glBindVertexArray(0);

    glDisable(GL_BLEND);
}

} // namespace Harmonia

#include "StarField.h"
#include <random>
#include <glm/gtc/type_ptr.hpp>

using namespace juce::gl;

namespace Harmonia {

void StarField::initialise(juce::OpenGLContext& ctx, int numStars) {
    ctx_   = &ctx;
    count_ = numStars;

    // Generate random stars on a large sphere
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> angle(0.f, 6.283185f);
    std::uniform_real_distribution<float> cosAngle(-1.f, 1.f);
    std::uniform_real_distribution<float> bright(0.3f, 1.f);

    std::vector<StarVertex> verts;
    verts.reserve((size_t)numStars);
    for (int i = 0; i < numStars; ++i) {
        float cosEl = cosAngle(rng);
        float sinEl = std::sqrt(1.f - cosEl * cosEl);
        float az    = angle(rng);
        float r     = 800.f;  // far enough to feel like sky
        verts.push_back({r * sinEl * std::cos(az),
                         r * cosEl,
                         r * sinEl * std::sin(az),
                         bright(rng)});
    }

    ctx.extensions.glGenVertexArrays(1, &vao_);
    ctx.extensions.glBindVertexArray(vao_);

    ctx.extensions.glGenBuffers(1, &vbo_);
    ctx.extensions.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ctx.extensions.glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(verts.size() * sizeof(StarVertex)),
        verts.data(), GL_STATIC_DRAW);

    // aPos = loc 0
    ctx.extensions.glEnableVertexAttribArray(0);
    ctx.extensions.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(StarVertex), (void*)0);
    // aBrightness = loc 1
    ctx.extensions.glEnableVertexAttribArray(1);
    ctx.extensions.glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE,
        sizeof(StarVertex), (void*)(3 * sizeof(float)));

    ctx.extensions.glBindVertexArray(0);
}

void StarField::shutdown() {
    vao_ = vbo_ = 0;
    count_ = 0;
}

void StarField::draw(juce::OpenGLShaderProgram& shader, const glm::mat4& vp) {
    if (!count_ || !vao_) return;

    shader.use();
    juce::OpenGLShaderProgram::Uniform uVP(shader, "uVP");
    uVP.setMatrix4(glm::value_ptr(vp), 1, GL_FALSE);

    glEnable(GL_PROGRAM_POINT_SIZE);
    ctx_->extensions.glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, count_);
    ctx_->extensions.glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

} // namespace Harmonia

#include "VoxelRenderer.h"
#include <juce_opengl/juce_opengl.h>
#include <glm/gtc/type_ptr.hpp>

// Use JUCE's GL namespace for all OpenGL constants and functions
using namespace juce::gl;

namespace Harmonia {

static const VoxelRenderer::Vertex kCubeVerts[24] = {
    {-0.5f,-0.5f,-0.5f, -1,0,0}, {-0.5f,-0.5f, 0.5f, -1,0,0},
    {-0.5f, 0.5f, 0.5f, -1,0,0}, {-0.5f, 0.5f,-0.5f, -1,0,0},
    { 0.5f,-0.5f, 0.5f,  1,0,0}, { 0.5f,-0.5f,-0.5f,  1,0,0},
    { 0.5f, 0.5f,-0.5f,  1,0,0}, { 0.5f, 0.5f, 0.5f,  1,0,0},
    {-0.5f,-0.5f,-0.5f, 0,-1,0}, { 0.5f,-0.5f,-0.5f, 0,-1,0},
    { 0.5f,-0.5f, 0.5f, 0,-1,0}, {-0.5f,-0.5f, 0.5f, 0,-1,0},
    {-0.5f, 0.5f, 0.5f, 0, 1,0}, { 0.5f, 0.5f, 0.5f, 0, 1,0},
    { 0.5f, 0.5f,-0.5f, 0, 1,0}, {-0.5f, 0.5f,-0.5f, 0, 1,0},
    { 0.5f,-0.5f,-0.5f, 0,0,-1}, {-0.5f,-0.5f,-0.5f, 0,0,-1},
    {-0.5f, 0.5f,-0.5f, 0,0,-1}, { 0.5f, 0.5f,-0.5f, 0,0,-1},
    {-0.5f,-0.5f, 0.5f, 0,0, 1}, { 0.5f,-0.5f, 0.5f, 0,0, 1},
    { 0.5f, 0.5f, 0.5f, 0,0, 1}, {-0.5f, 0.5f, 0.5f, 0,0, 1},
};

static const GLuint kCubeIndices[36] = {
     0, 1, 2,  0, 2, 3,  4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11, 12,13,14, 12,14,15,
    16,17,18, 16,18,19, 20,21,22, 20,22,23,
};

void VoxelRenderer::initialise(juce::OpenGLContext& ctx) {
    ctx_ = &ctx;

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // Cube geometry
    glGenBuffers(1, &cubeVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)(3 * sizeof(float)));

    // Index buffer
    glGenBuffers(1, &cubeEBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices),
                 kCubeIndices, GL_STATIC_DRAW);

    // Instance buffer (dynamic)
    glGenBuffers(1, &instanceVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Instance) * 4096, nullptr, GL_DYNAMIC_DRAW);

    // iOffset = loc 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Instance), (void*)0);
    glVertexAttribDivisor(2, 1);
    // iColour = loc 3
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          (void*)(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    // iGlow = loc 4
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          (void*)(7 * sizeof(float)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
}

void VoxelRenderer::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (cubeVBO_)    { glDeleteBuffers(1, &cubeVBO_);    cubeVBO_    = 0; }
    if (cubeEBO_)    { glDeleteBuffers(1, &cubeEBO_);    cubeEBO_    = 0; }
    if (instanceVBO_){ glDeleteBuffers(1, &instanceVBO_); instanceVBO_= 0; }
}

void VoxelRenderer::update(const VoxelGrid& grid) {
    instances_.clear();
    instances_.reserve(512);

    const int W = grid.width(), H = grid.height(), D = grid.depth();
    const float cx = W * 0.5f, cy = H * 0.5f, cz = D * 0.5f;

    for (int z = 0; z < D; ++z) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const auto& v = grid.at(x, y, z);
                if (v.state < kMinState) continue;

                juce::Colour col = CircleOfFifths::withActivation(
                    CircleOfFifths::colourForPitchClass(x % 12), v.state);

                instances_.push_back({
                    (float)x - cx, (float)y - cy, (float)z - cz,
                    col.getFloatRed(), col.getFloatGreen(), col.getFloatBlue(),
                    juce::jlimit(0.f, 1.f, v.state * 1.5f),
                    v.state
                });
            }
        }
    }

    instanceCount_ = (int)instances_.size();

    // Upload to GPU
    if (instanceCount_ > 0 && instanceVBO_) {
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
            (GLsizeiptr)(instances_.size() * sizeof(Instance)),
            instances_.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void VoxelRenderer::draw(juce::OpenGLShaderProgram& shader,
                         const glm::mat4& vp,
                         const glm::vec3& camPos,
                         float time,
                         float voxelSize)
{
    if (instanceCount_ == 0 || !vao_) return;

    shader.use();

    // Set uniforms using JUCE's Uniform helper
    juce::OpenGLShaderProgram::Uniform uVP     (shader, "uVP");
    juce::OpenGLShaderProgram::Uniform uVSz    (shader, "uVoxelSize");
    juce::OpenGLShaderProgram::Uniform uCam    (shader, "uCamPos");
    juce::OpenGLShaderProgram::Uniform uTime   (shader, "uTime");
    juce::OpenGLShaderProgram::Uniform uSun    (shader, "uSunDir");

    uVP.setMatrix4(glm::value_ptr(vp), 1, GL_FALSE);
    uVSz.set(voxelSize);
    uCam.set(camPos.x, camPos.y, camPos.z);
    uTime.set(time);
    glm::vec3 sun = glm::normalize(glm::vec3(0.5f, 1.f, 0.3f));
    uSun.set(sun.x, sun.y, sun.z);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr, instanceCount_);
    glBindVertexArray(0);
}

} // namespace Harmonia

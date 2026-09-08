#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <memory>

namespace Harmonia {
class GroundPlane {
public:
    GroundPlane();
    ~GroundPlane();

    void render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx);

    // Terrain height at a world (x, z) position - the same layered-noise
    // function the mesh itself is built from. Static and pure so movement/
    // collision code can query it without needing a live GroundPlane
    // instance or touching the renderer.
    static float heightAt(float worldX, float worldZ);

private:
    void init(juce::OpenGLContext& ctx);

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    int indexCount_ = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
};
}

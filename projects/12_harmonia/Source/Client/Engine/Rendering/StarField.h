#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>

namespace Harmonia {

class StarField {
public:
    void initialise(juce::OpenGLContext& ctx, int numStars = 2000);
    void shutdown();
    // skyRotation rotates the star sphere so it turns in sync with the
    // sun's own day-arc (Sun::skyRotation()) - one coherent rotating sky
    // instead of a moving sun over a frozen star field.
    void draw(juce::OpenGLShaderProgram& shader, const glm::mat4& vp, const glm::mat4& skyRotation);

private:
    struct StarVertex { float x, y, z, brightness; };

    GLuint vao_     = 0;
    GLuint vbo_     = 0;
    int    count_   = 0;
    juce::OpenGLContext* ctx_ = nullptr;
};

} // namespace Harmonia

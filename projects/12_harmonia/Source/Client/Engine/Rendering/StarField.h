#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>

namespace Harmonia {
class StarField {
public:
    void initialise(juce::OpenGLContext& ctx, int numStars = 2000);
    void shutdown();
    void render(const glm::mat4& view, const glm::mat4& proj);
private:
    GLuint vao_ = 0, vbo_ = 0;
    int numStars_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
};
}

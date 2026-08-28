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
    
private:
    void init(juce::OpenGLContext& ctx);

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
};
}

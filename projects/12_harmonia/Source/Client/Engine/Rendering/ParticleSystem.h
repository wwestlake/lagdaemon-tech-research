#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>

namespace Harmonia {
struct Particle {
    glm::vec3 pos, vel;
    glm::vec4 colour;
    float life;
    float maxLife;
    float size;
};

class ParticleSystem {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    void update(float dt);
    void render(const glm::mat4& view, const glm::mat4& proj);
    void burst(glm::vec3 position, juce::Colour colour, int count = 24);
private:
    std::vector<Particle> particles_;
    GLuint vbo_ = 0, vao_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
};
}

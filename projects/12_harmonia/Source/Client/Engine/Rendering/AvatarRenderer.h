#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <map>
#include "Shared/World/WorldState.h"

namespace Harmonia {
class AvatarRenderer {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    void render(const std::map<uint32_t, PlayerState>& players,
                const glm::mat4& view, const glm::mat4& proj,
                double timeSeconds);
    void noteTriggered(uint32_t playerID);
private:
    GLuint sphereVAO_ = 0, sphereVBO_ = 0, sphereEBO_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
    std::map<uint32_t, float> pulseTimes_;
};
}

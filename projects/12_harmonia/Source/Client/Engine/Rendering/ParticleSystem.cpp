#include "ParticleSystem.h"

namespace Harmonia {
void ParticleSystem::initialise(juce::OpenGLContext& ctx) {}
void ParticleSystem::shutdown() {}
void ParticleSystem::update(float dt) {}
void ParticleSystem::render(const glm::mat4& view, const glm::mat4& proj) {}
void ParticleSystem::burst(glm::vec3 position, juce::Colour colour, int count) {}
}

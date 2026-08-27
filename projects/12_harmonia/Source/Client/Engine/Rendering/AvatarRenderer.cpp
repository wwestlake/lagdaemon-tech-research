#include "AvatarRenderer.h"

namespace Harmonia {
void AvatarRenderer::initialise(juce::OpenGLContext& ctx) {}
void AvatarRenderer::shutdown() {}
void AvatarRenderer::render(const std::map<uint32_t, PlayerState>& players, const glm::mat4& view, const glm::mat4& proj, double timeSeconds) {}
void AvatarRenderer::noteTriggered(uint32_t playerID) {}
}

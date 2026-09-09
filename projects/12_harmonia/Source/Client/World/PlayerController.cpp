#include "PlayerController.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonia {
PlayerController::PlayerController() {}

glm::vec3 PlayerController::computeMoveDir(float cameraFacingAngle) const {
    glm::vec3 forward = glm::vec3(-std::sin(cameraFacingAngle), 0.0f, std::cos(cameraFacingAngle));
    glm::vec3 right = glm::vec3(std::cos(cameraFacingAngle), 0.0f, std::sin(cameraFacingAngle));

    glm::vec3 move(0.0f);
    if (juce::KeyPress::isKeyCurrentlyDown('W')) move += forward;
    if (juce::KeyPress::isKeyCurrentlyDown('S')) move -= forward;
    if (juce::KeyPress::isKeyCurrentlyDown('A')) move -= right;
    if (juce::KeyPress::isKeyCurrentlyDown('D')) move += right;

    if (glm::length(move) > 0.0f) move = glm::normalize(move);
    return move;
}

bool PlayerController::jumpHeld() const {
    return juce::KeyPress::isKeyCurrentlyDown('E') || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey);
}

void PlayerController::setPosition(const glm::vec3& p) {
    pos_ = p;
    dirty_ = true;
}

void PlayerController::mouseMove(float dx, float dy) {
    yaw_ += dx * 0.01f;
    dirty_ = true;
}

glm::vec3 PlayerController::position() const { return pos_; }
float PlayerController::yaw() const { return yaw_; }
bool PlayerController::dirty() const { return dirty_; }
void PlayerController::clearDirty() { dirty_ = false; }
}

#include "PlayerController.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonia {
PlayerController::PlayerController() {}

glm::vec3 PlayerController::computeMoveDir(float cameraAzimuth, bool inputEnabled) const {
    glm::vec3 move(0.0f);
    if (!inputEnabled) return move;
    
    // forward is pointing to +Z in JUCE camera space.
    // azimuth = 0 faces +Z.
    glm::vec3 forward = glm::vec3(-std::sin(cameraAzimuth), 0.0f, std::cos(cameraAzimuth));
    glm::vec3 right = glm::vec3(-std::cos(cameraAzimuth), 0.0f, -std::sin(cameraAzimuth));
    
    if (juce::KeyPress::isKeyCurrentlyDown('W')) move += forward;
    if (juce::KeyPress::isKeyCurrentlyDown('S')) move -= forward;
    if (juce::KeyPress::isKeyCurrentlyDown('A')) move += right; // 'right' points Left relative to camera view
    if (juce::KeyPress::isKeyCurrentlyDown('D')) move -= right;

    if (glm::length(move) > 0.0f) move = glm::normalize(move);
    return move;
}

bool PlayerController::jumpHeld(bool inputEnabled) const {
    if (!inputEnabled) return false;
    return juce::KeyPress::isKeyCurrentlyDown('E') || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey);
}

bool PlayerController::runHeld(bool inputEnabled) const {
    if (!inputEnabled) return false;
    return juce::ModifierKeys::getCurrentModifiers().isShiftDown();
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

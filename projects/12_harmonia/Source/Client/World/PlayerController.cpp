#include "PlayerController.h"
#include <cmath>

namespace Harmonia {
PlayerController::PlayerController() {}

void PlayerController::update(float dt, const std::set<int>& keysDown) {
    // Basic WASD logic stub
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

#pragma once
#include <glm/glm.hpp>
#include <set>

namespace Harmonia {
class PlayerController {
public:
    PlayerController();

    // Reads WASD key state ONLY - no position mutation, real movement
    // now happens inside PhysicsWorld (gravity, ground collision, slope
    // handling). cameraFacingAngle is the same angle OpenWorld already
    // derives for the character's own facing.
    glm::vec3 computeMoveDir(float cameraFacingAngle) const;
    bool jumpHeld() const;

    void mouseMove(float dx, float dy);

    glm::vec3 position() const;
    void setPosition(const glm::vec3& p);
    float yaw() const;
    bool dirty() const;
    void clearDirty();

private:
    glm::vec3 pos_ = {0, 2, 0};
    float yaw_ = 0.f;
    bool dirty_ = false;
};
}

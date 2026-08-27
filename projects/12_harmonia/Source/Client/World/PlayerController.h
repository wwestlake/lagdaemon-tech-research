#pragma once
#include <glm/glm.hpp>
#include <set>

namespace Harmonia {
class PlayerController {
public:
    PlayerController();
    void update(float dt, const std::set<int>& keysDown);
    void mouseMove(float dx, float dy);
    
    glm::vec3 position() const;
    float yaw() const;
    bool dirty() const;
    void clearDirty();
    
private:
    glm::vec3 pos_ = {0, 2, 0};
    float yaw_ = 0.f;
    float speed_ = 10.f;
    bool dirty_ = false;
};
}

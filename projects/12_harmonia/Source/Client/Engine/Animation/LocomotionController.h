#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace djehuti {
namespace animation {

class LocomotionController {
public:
    LocomotionController();

    void init(const glm::vec3& startPos);

    void update(const glm::vec3& currentPelvisPos, const glm::vec3& velocity, float yaw, float dt,
                float (*groundRaycast)(float x, float z, void* userData), void* userData);

    glm::vec3 getLeftFootTarget() const { return leftFoot_.currentPos; }
    glm::vec3 getRightFootTarget() const { return rightFoot_.currentPos; }
    float getPelvisDrop() const { return pelvisDrop_; }

    void setTreadmillMode(bool enable) { treadmillMode_ = enable; }
    
private:
    bool treadmillMode_ = false;
    
    // Core parameters from design
    float phase_ = 0.0f;           // Normalized 0.0 to 1.0
    float cadence_ = 1.8f;         // Steps per second (will be calculated dynamically)
    float baseStrideLength_ = 0.8f;// Meters
    float stepHeight_ = 0.15f;     // Meters
    float stanceWidth_ = 0.3f;     // Meters

    struct Foot {
        bool isStance = true;
        glm::vec3 currentPos{0.0f};
        glm::vec3 lockedPos{0.0f};     // Stance lock
        glm::vec3 swingStart{0.0f};
        glm::vec3 swingTarget{0.0f};
    };

    Foot leftFoot_;
    Foot rightFoot_;
    float pelvisDrop_ = 0.0f;

    glm::vec3 evaluateSwingArc(const glm::vec3& start, const glm::vec3& target, float s, float height) const;
};

} // namespace animation
} // namespace djehuti

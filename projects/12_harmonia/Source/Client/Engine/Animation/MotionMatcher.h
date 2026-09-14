#pragma once

#include "MotionDatabase.h"
#include <glm/glm.hpp>
#include <memory>

namespace djehuti {
namespace animation {

struct MotionIntent {
    glm::vec3 desiredVelocity{0.0f};
    float desiredFacingYaw = 0.0f;
    float turnRate = 0.0f;
};

class MotionMatcher {
public:
    MotionMatcher();
    void setDatabase(std::shared_ptr<MotionDatabase> db);
    
    // Updates the motion matching state and returns the target pose parameters
    // that the LocomotionController will use.
    MotionSample update(const MotionIntent& intent, float dt);
    
private:
    std::shared_ptr<MotionDatabase> db_;
    float currentPhase_ = 0.0f;
    const MotionRecord* currentRecord_ = nullptr;
};

} // namespace animation
} // namespace djehuti

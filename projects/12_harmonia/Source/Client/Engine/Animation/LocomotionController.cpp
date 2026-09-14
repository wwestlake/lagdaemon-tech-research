#include "LocomotionController.h"
#include <algorithm>
#include <cmath>

namespace djehuti {
namespace animation {

LocomotionController::LocomotionController() {}

void LocomotionController::init(const glm::vec3& startPos) {
    // We assume facing +Z for initialization
    glm::vec3 offsetL(-stanceWidth_ * 0.5f, 0.0f, 0.0f);
    glm::vec3 offsetR(stanceWidth_ * 0.5f, 0.0f, 0.0f);
    
    leftFoot_.lockedPos = startPos + offsetL;
    leftFoot_.currentPos = leftFoot_.lockedPos;
    leftFoot_.isStance = true;

    rightFoot_.lockedPos = startPos + offsetR;
    rightFoot_.currentPos = rightFoot_.lockedPos;
    rightFoot_.isStance = true;
    
    phase_ = 0.0f;
}

glm::vec3 LocomotionController::evaluateSwingArc(const glm::vec3& start, const glm::vec3& target, float s, float height) const {
    glm::vec3 pos = start + (target - start) * s;
    pos.y += std::sin(s * 3.14159265f) * height;
    return pos;
}

void LocomotionController::update(const glm::vec3& currentPelvisPos, const glm::vec3& velocity, float yaw, float dt,
                                float (*groundRaycast)(float x, float z, void* userData), void* userData) {
    float speed = glm::length(glm::vec2(velocity.x, velocity.z));
    bool isMoving = speed > 0.1f;
    
    // Dynamic cadence based on speed (clamped to prevent division by zero or crazy fast steps)
    if (isMoving) {
        cadence_ = std::max(1.0f, speed / baseStrideLength_);
        phase_ += cadence_ * dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
    } else {
        // Soft reset to stance if we stop
        cadence_ = 1.0f;
    }
    
    // Treadmill mode: move the locked positions backward mathematically
    if (treadmillMode_ && isMoving) {
        if (leftFoot_.isStance) leftFoot_.lockedPos -= velocity * dt;
        if (rightFoot_.isStance) rightFoot_.lockedPos -= velocity * dt;
    }

    // Facing vectors
    glm::vec3 forward(-std::sin(yaw), 0.0f, std::cos(yaw));
    glm::vec3 right(std::cos(yaw), 0.0f, std::sin(yaw));
    
    // Phase intervals
    // Left: Stance [0.0, 0.6), Swing [0.6, 1.0)
    // Right: Stance [0.5, 0.1) -> means Swing [0.1, 0.5)
    bool leftShouldBeStance = (phase_ >= 0.0f && phase_ < 0.6f) || !isMoving;
    bool rightShouldBeStance = (phase_ < 0.1f || phase_ >= 0.5f) || !isMoving;
    
    auto updateFoot = [&](Foot& foot, bool shouldBeStance, float swingStartPhase, float swingEndPhase, bool isLeft) {
        if (shouldBeStance) {
            if (!foot.isStance) {
                // Just landed!
                foot.isStance = true;
                foot.lockedPos = foot.swingTarget;
            }
            foot.currentPos = foot.lockedPos;
        } else {
            if (foot.isStance) {
                // Just took off!
                foot.isStance = false;
                foot.swingStart = foot.lockedPos;
                
                // Predict target using Linear Inverted Pendulum Model (LIPM)
                glm::vec3 offset = right * (isLeft ? -stanceWidth_ * 0.5f : stanceWidth_ * 0.5f);
                float swingDurationInSeconds = (swingEndPhase - swingStartPhase) / cadence_;
                if (swingEndPhase < swingStartPhase) swingDurationInSeconds = (swingEndPhase + 1.0f - swingStartPhase) / cadence_;
                
                float g = 9.81f;
                float z0 = std::max(0.1f, currentPelvisPos.y - foot.lockedPos.y); // effective leg height
                float omega0 = std::sqrt(g / z0);
                
                glm::vec3 comPos = currentPelvisPos;
                glm::vec3 comVel = velocity;
                
                // LIPM Capture Point calculation
                // p_land = x_0 * cosh(w*t) + (v_0/w)*sinh(w*t)
                // For a robust procedural walk, we use the simplified capture point + stance offset
                glm::vec3 predictedRoot = comPos + (comVel / omega0) * std::sinh(omega0 * swingDurationInSeconds);
                
                foot.swingTarget = predictedRoot + offset;
                
                if (groundRaycast) {
                    foot.swingTarget.y = groundRaycast(foot.swingTarget.x, foot.swingTarget.z, userData);
                } else {
                    foot.swingTarget.y = currentPelvisPos.y;
                }
            }
            
            // Calculate normalized swing progress 's'
            float s = 0.0f;
            if (swingEndPhase > swingStartPhase) {
                s = (phase_ - swingStartPhase) / (swingEndPhase - swingStartPhase);
            } else {
                // Wrapping phase (Right foot)
                float adjustedPhase = phase_;
                if (phase_ < swingStartPhase) adjustedPhase += 1.0f;
                float duration = (swingEndPhase + 1.0f) - swingStartPhase;
                s = (adjustedPhase - swingStartPhase) / duration;
            }
            s = std::clamp(s, 0.0f, 1.0f);
            
            foot.currentPos = evaluateSwingArc(foot.swingStart, foot.swingTarget, s, stepHeight_);
        }
    };
    
    updateFoot(leftFoot_, leftShouldBeStance, 0.6f, 1.0f, true);
    updateFoot(rightFoot_, rightShouldBeStance, 0.1f, 0.5f, false);
    
    // Pelvis Drop
    float footSpread = glm::distance(leftFoot_.currentPos, rightFoot_.currentPos);
    pelvisDrop_ = std::min(footSpread * 0.15f, 0.2f);
}

} // namespace animation
} // namespace djehuti

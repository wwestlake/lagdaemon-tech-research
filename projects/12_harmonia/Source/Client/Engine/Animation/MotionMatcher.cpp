#include "MotionMatcher.h"

namespace djehuti {
namespace animation {

MotionMatcher::MotionMatcher() {}

void MotionMatcher::setDatabase(std::shared_ptr<MotionDatabase> db) {
    db_ = db;
}

MotionSample MotionMatcher::update(const MotionIntent& intent, float dt) {
    if (!db_) return MotionSample();
    
    float speed = glm::length(intent.desiredVelocity);
    auto candidates = db_->getCandidates(speed);
    
    if (candidates.empty()) return MotionSample();
    
    // For now, if we have 1 candidate, just evaluate it
    if (candidates.size() == 1) {
        currentRecord_ = candidates[0];
        currentPhase_ += currentRecord_->cadence * dt;
        if (currentPhase_ >= 1.0f) currentPhase_ -= 1.0f;
        return currentRecord_->evaluate(currentPhase_);
    }
    
    // If we have 2 candidates, blend them based on speed
    const auto* rA = candidates[0];
    const auto* rB = candidates[1];
    
    float speedRange = rB->speed - rA->speed;
    float weightB = 0.0f;
    if (speedRange > 0.001f) {
        weightB = (speed - rA->speed) / speedRange;
    }
    
    float blendedCadence = rA->cadence * (1.0f - weightB) + rB->cadence * weightB;
    currentPhase_ += blendedCadence * dt;
    if (currentPhase_ >= 1.0f) currentPhase_ -= 1.0f;
    
    MotionSample sA = rA->evaluate(currentPhase_);
    MotionSample sB = rB->evaluate(currentPhase_);
    
    MotionSample result;
    result.phase = currentPhase_;
    
    for (const auto& [jointId, rotA] : sA.jointRotations) {
        auto itB = sB.jointRotations.find(jointId);
        if (itB != sB.jointRotations.end()) {
            result.jointRotations[jointId] = glm::slerp(rotA, itB->second, weightB);
        } else {
            result.jointRotations[jointId] = rotA;
        }
    }
    
    result.features.leftFootPos = glm::mix(sA.features.leftFootPos, sB.features.leftFootPos, weightB);
    result.features.rightFootPos = glm::mix(sA.features.rightFootPos, sB.features.rightFootPos, weightB);
    result.features.velocity = glm::mix(sA.features.velocity, sB.features.velocity, weightB);
    
    return result;
}

} // namespace animation
} // namespace djehuti

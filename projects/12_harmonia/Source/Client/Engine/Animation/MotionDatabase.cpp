#include "MotionDatabase.h"
#include <algorithm>
#include <cmath>

namespace djehuti {
namespace animation {

MotionSample MotionRecord::evaluate(float phase) const {
    if (samples.empty()) return MotionSample();
    
    // Ensure phase is wrapped [0, 1)
    phase = std::fmod(phase, 1.0f);
    if (phase < 0.0f) phase += 1.0f;
    
    // Find bounding samples
    float scaledPhase = phase * (samples.size() - 1);
    int indexA = static_cast<int>(scaledPhase);
    int indexB = std::min(indexA + 1, static_cast<int>(samples.size() - 1));
    float t = scaledPhase - indexA;
    
    const auto& a = samples[indexA];
    const auto& b = samples[indexB];
    
    MotionSample result;
    result.phase = phase;
    
    // Interpolate rotations
    for (const auto& [jointId, rotA] : a.jointRotations) {
        auto itB = b.jointRotations.find(jointId);
        if (itB != b.jointRotations.end()) {
            result.jointRotations[jointId] = glm::slerp(rotA, itB->second, t);
        } else {
            result.jointRotations[jointId] = rotA;
        }
    }
    
    // Interpolate features
    result.features.leftFootPos = glm::mix(a.features.leftFootPos, b.features.leftFootPos, t);
    result.features.leftFootVel = glm::mix(a.features.leftFootVel, b.features.leftFootVel, t);
    result.features.rightFootPos = glm::mix(a.features.rightFootPos, b.features.rightFootPos, t);
    result.features.rightFootVel = glm::mix(a.features.rightFootVel, b.features.rightFootVel, t);
    result.features.velocity = glm::mix(a.features.velocity, b.features.velocity, t);
    
    // Discrete contacts (threshold at 0.5)
    result.features.leftContact = (t < 0.5f) ? a.features.leftContact : b.features.leftContact;
    result.features.rightContact = (t < 0.5f) ? a.features.rightContact : b.features.rightContact;
    
    return result;
}

void MotionDatabase::addRecord(const MotionRecord& record) {
    records_.push_back(record);
    // Sort by speed for easy matching
    std::sort(records_.begin(), records_.end(), [](const MotionRecord& a, const MotionRecord& b) {
        return a.speed < b.speed;
    });
}

std::vector<const MotionRecord*> MotionDatabase::getCandidates(float desiredSpeed) const {
    if (records_.empty()) return {};
    
    // Simple 1D nearest neighbor on speed for now
    std::vector<const MotionRecord*> candidates;
    
    if (desiredSpeed <= records_.front().speed) {
        candidates.push_back(&records_.front());
        return candidates;
    }
    if (desiredSpeed >= records_.back().speed) {
        candidates.push_back(&records_.back());
        return candidates;
    }
    
    for (size_t i = 0; i < records_.size() - 1; ++i) {
        if (desiredSpeed >= records_[i].speed && desiredSpeed <= records_[i+1].speed) {
            candidates.push_back(&records_[i]);
            candidates.push_back(&records_[i+1]);
            break;
        }
    }
    
    return candidates;
}

} // namespace animation
} // namespace djehuti

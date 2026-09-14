#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace djehuti {
namespace animation {

std::pair<glm::quat, glm::quat> IKSolver::solveTwoBoneIK(
    const glm::vec3& rootPos,
    const glm::vec3& targetPos,
    const glm::vec3& poleDir,
    float bone1Len,
    float bone2Len,
    const glm::vec3& defaultBoneDir
) {
    glm::vec3 effectorDir = targetPos - rootPos;
    float dist = glm::length(effectorDir);
    
    // Normalize effector direction or fallback
    if (dist > 0.0001f) {
        effectorDir /= dist;
    } else {
        effectorDir = glm::normalize(defaultBoneDir);
    }
    
    // Clamp target distance to maximum reach
    dist = std::min(dist, bone1Len + bone2Len - 0.001f);
    
    float cosKnee = (bone1Len * bone1Len + bone2Len * bone2Len - dist * dist) / (2.0f * bone1Len * bone2Len);
    cosKnee = std::clamp(cosKnee, -1.0f, 1.0f);
    float kneeAngle = std::acos(cosKnee);
    
    float cosHip = (bone1Len * bone1Len + dist * dist - bone2Len * bone2Len) / (2.0f * bone1Len * dist);
    cosHip = std::clamp(cosHip, -1.0f, 1.0f);
    float hipAngle = std::acos(cosHip);
    
    glm::vec3 bendAxis = glm::cross(poleDir, effectorDir);
    if (glm::length(bendAxis) < 0.001f) {
        bendAxis = glm::vec3(1, 0, 0);
    } else {
        bendAxis = glm::normalize(bendAxis);
    }
    
    glm::quat aimRot = glm::rotation(glm::normalize(defaultBoneDir), effectorDir); 
    glm::quat hipRot = glm::angleAxis(hipAngle, bendAxis) * aimRot;
    
    // The knee bends around the bendAxis
    float kneeBend = 3.14159265f - kneeAngle;
    glm::quat kneeRot = glm::angleAxis(kneeBend, bendAxis);
    
    return {hipRot, kneeRot};
}

} // namespace animation
} // namespace djehuti

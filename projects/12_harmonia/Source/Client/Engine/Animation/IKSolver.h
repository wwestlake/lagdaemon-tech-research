#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

namespace djehuti {
namespace animation {

class IKSolver {
public:
    static std::pair<glm::quat, glm::quat> solveTwoBoneIK(
        const glm::vec3& rootPos,
        const glm::vec3& targetPos,
        const glm::vec3& poleDir,
        float bone1Len,
        float bone2Len,
        const glm::vec3& defaultBoneDir = glm::vec3(0, -1, 0)
    );
};

} // namespace animation
} // namespace djehuti

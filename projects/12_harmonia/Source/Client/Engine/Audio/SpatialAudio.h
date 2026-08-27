#pragma once
#include <glm/glm.hpp>
#include <utility>

namespace Harmonia {
class SpatialAudio {
public:
    static std::pair<float,float> panGains(glm::vec3 listener, glm::vec3 source, float listenerYaw, float maxDistance = 50.f);
    static float distanceGain(glm::vec3 listener, glm::vec3 source, float maxDistance = 50.f);
};
}

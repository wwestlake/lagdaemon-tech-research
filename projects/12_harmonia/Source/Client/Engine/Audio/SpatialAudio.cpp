#include "SpatialAudio.h"

namespace Harmonia {
std::pair<float,float> SpatialAudio::panGains(glm::vec3 listener, glm::vec3 source, float listenerYaw, float maxDistance) {
    return {0.5f, 0.5f}; // Stub
}

float SpatialAudio::distanceGain(glm::vec3 listener, glm::vec3 source, float maxDistance) {
    return 1.0f; // Stub
}
}

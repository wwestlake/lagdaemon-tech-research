#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace djehuti {
namespace animation {

struct FeatureVector {
    glm::vec3 leftFootPos{0.f};
    glm::vec3 leftFootVel{0.f};
    glm::vec3 rightFootPos{0.f};
    glm::vec3 rightFootVel{0.f};
    glm::vec3 velocity{0.f};
    
    // Extracted contacts
    bool leftContact = false;
    bool rightContact = false;
};

struct MotionSample {
    float phase;
    std::unordered_map<int, glm::quat> jointRotations;
    FeatureVector features;
};

struct MotionRecord {
    std::string name;
    float originalDuration;
    float cadence; // Calculated steps per second
    float speed;   // Meters per second
    
    std::vector<MotionSample> samples; // Normalized over phase [0, 1]
    
    // Evaluate pose and features at a given phase
    MotionSample evaluate(float phase) const;
};

class MotionDatabase {
public:
    MotionDatabase() = default;
    
    // Add a pre-processed record
    void addRecord(const MotionRecord& record);
    
    // Find the best blending candidates
    std::vector<const MotionRecord*> getCandidates(float desiredSpeed) const;

private:
    std::vector<MotionRecord> records_;
};

} // namespace animation
} // namespace djehuti

#pragma once
#include <juce_core/juce_core.h>
#include <glm/glm.hpp>
#include <juce_graphics/juce_graphics.h>

namespace Harmonia {
class RemotePlayer {
public:
    RemotePlayer(uint32_t id, const juce::String& name, float colorHue);
    
    void updatePosition(float x, float y, float z, float yaw);
    void noteOn(int midiNote, float velocity);
    void update(float dt);
    
    uint32_t playerID() const;
    juce::String playerName() const;
    glm::vec3 position() const;
    float yaw() const;
    juce::Colour colour() const;
    bool isPulsing() const;
    
private:
    uint32_t id_;
    juce::String name_;
    float colorHue_;
    glm::vec3 targetPos_, currentPos_;
    float targetYaw_, currentYaw_;
    float pulseTime_ = 0.f;
};
}

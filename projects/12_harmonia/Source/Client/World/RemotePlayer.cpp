#include "RemotePlayer.h"

namespace Harmonia {
RemotePlayer::RemotePlayer(uint32_t id, const juce::String& name, float colorHue)
    : id_(id), name_(name), colorHue_(colorHue) {}

void RemotePlayer::updatePosition(float x, float y, float z, float yaw) {
    targetPos_ = {x, y, z};
    targetYaw_ = yaw;
}

void RemotePlayer::noteOn(int midiNote, float velocity) {
    pulseTime_ = 1.0f;
}

void RemotePlayer::update(float dt) {
    currentPos_ += (targetPos_ - currentPos_) * 10.0f * dt;
    currentYaw_ += (targetYaw_ - currentYaw_) * 10.0f * dt;
    if (pulseTime_ > 0.f) pulseTime_ -= dt;
}

uint32_t RemotePlayer::playerID() const { return id_; }
juce::String RemotePlayer::playerName() const { return name_; }
glm::vec3 RemotePlayer::position() const { return currentPos_; }
float RemotePlayer::yaw() const { return currentYaw_; }
juce::Colour RemotePlayer::colour() const { return juce::Colour(colorHue_, 1.0f, 1.0f, 1.0f); }
bool RemotePlayer::isPulsing() const { return pulseTime_ > 0.f; }
}

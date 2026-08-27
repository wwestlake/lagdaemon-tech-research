#include "OpenWorld.h"

namespace Harmonia {
OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, juce::OpenGLContext* ctx)
    : worldState_(state), audio_(audio), net_(net) {}

void OpenWorld::update(float dt, const std::set<int>& keysDown, float mouseDx, float mouseDy) {
    localPlayer_.mouseMove(mouseDx, mouseDy);
    localPlayer_.update(dt, keysDown);
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::onPlayerJoined(uint32_t id, const juce::String& name, float hue, glm::vec3 pos) {
    remotePlayers_[id] = std::make_unique<RemotePlayer>(id, name, hue);
    remotePlayers_[id]->updatePosition(pos.x, pos.y, pos.z, 0.0f);
}

void OpenWorld::onPlayerLeft(uint32_t id) { remotePlayers_.erase(id); }

void OpenWorld::onPlayerPosition(uint32_t id, glm::vec3 pos, float yaw) {
    if (remotePlayers_.count(id)) remotePlayers_[id]->updatePosition(pos.x, pos.y, pos.z, yaw);
}

void OpenWorld::onNoteOn(uint32_t playerID, int midiNote, float vel) {
    if (remotePlayers_.count(playerID)) remotePlayers_[playerID]->noteOn(midiNote, vel);
}

void OpenWorld::onNoteOff(uint32_t playerID, int midiNote) {}

Camera& OpenWorld::camera() { return camera_; }
PlayerController& OpenWorld::localPlayer() { return localPlayer_; }

void OpenWorld::detectRegion() {}
}

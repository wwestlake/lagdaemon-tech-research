#pragma once
#include <map>
#include <set>
#include <vector>
#include <memory>
#include "Client/Engine/Rendering/Camera.h"
#include "PlayerController.h"
#include "RemotePlayer.h"
#include "Regions/IRegion.h"
#include "Client/Engine/Rendering/AvatarRenderer.h"
#include "Client/Engine/Rendering/StarField.h"
#include "Client/Engine/Rendering/ParticleSystem.h"
#include "Client/Engine/Rendering/PostProcess.h"
#include "Client/Engine/Rendering/GroundPlane.h"
#include "Client/Engine/Audio/AudioEngine.h"
#include "Client/Engine/Audio/MidiEngine.h"
#include "Shared/World/WorldState.h"

namespace Harmonia {
namespace Net { class NetworkClient; }

class OpenWorld {
public:
    OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi,
              Net::NetworkClient* net, juce::OpenGLContext* ctx);
    
    void update(float dt, const std::set<int>& keysDown, float mouseDx, float mouseDy);
    void render(const glm::mat4& view, const glm::mat4& proj);
    void render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx);
    
    void onPlayerJoined(uint32_t id, const juce::String& name, float hue, glm::vec3 pos);
    void onPlayerLeft(uint32_t id);
    void onPlayerPosition(uint32_t id, glm::vec3 pos, float yaw);
    void onNoteOn(uint32_t playerID, int midiNote, float vel);
    void onNoteOff(uint32_t playerID, int midiNote);
    
    Camera& camera();
    PlayerController& localPlayer();
    
private:
    void detectRegion();
    
    WorldState* worldState_;
    AudioEngine* audio_;
    Net::NetworkClient* net_;
    
    PlayerController localPlayer_;
    Camera camera_;
    
    std::map<uint32_t, std::unique_ptr<RemotePlayer>> remotePlayers_;
    std::vector<std::unique_ptr<IRegion>> regions_;
    IRegion* currentRegion_ = nullptr;
    
    std::unique_ptr<AvatarRenderer> avatarRenderer_;
    std::unique_ptr<StarField> starField_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<PostProcess> postProcess_;
    std::unique_ptr<GroundPlane> ground_;
};
}

#pragma once
#include <map>
#include <set>
#include <string>
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
#include "Client/Engine/Rendering/Terrain.h"
#include "Client/Engine/Rendering/GltfCharacter.h"
#include "Client/Physics/PhysicsWorld.h"
#include "Client/Engine/Audio/AudioEngine.h"
#include "Client/Engine/Audio/MidiEngine.h"
#include "Shared/World/WorldState.h"

namespace djehuti { namespace animation { class LocomotionController; } }

namespace Harmonia {
namespace Net { class NetworkClient; }

class OpenWorld {
public:
    OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi,
              Net::NetworkClient* net, Camera& camera);
    ~OpenWorld();
    
    void update(float dt);
    void render(const glm::mat4& view, const glm::mat4& proj);
    void render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                const glm::vec3& sunDir, const glm::vec3& sunColor);
    
    void onPlayerJoined(uint32_t id, const juce::String& name, float hue, glm::vec3 pos);
    void onPlayerLeft(uint32_t id);
    void onPlayerPosition(uint32_t id, glm::vec3 pos, float yaw);
    void onNoteOn(uint32_t playerID, int midiNote, float vel);
    void onNoteOff(uint32_t playerID, int midiNote);
    
    Camera& camera();
    PlayerController& localPlayer();
    // Tab-toggle between first and third person, per the user's request -
    // just flips the mode; render()/update() already branch on it.
    void toggleFirstPerson();

private:
    void detectRegion();
    
    WorldState* worldState_;
    AudioEngine* audio_;
    Net::NetworkClient* net_;
    
    PlayerController localPlayer_;
    Camera& camera_;
    
    std::map<uint32_t, std::unique_ptr<RemotePlayer>> remotePlayers_;
    std::vector<std::unique_ptr<IRegion>> regions_;
    IRegion* currentRegion_ = nullptr;
    
    std::unique_ptr<AvatarRenderer> avatarRenderer_;
    std::unique_ptr<StarField> starField_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<PostProcess> postProcess_;
    std::unique_ptr<Terrain> ground_;
    std::unique_ptr<GltfCharacter> playerChar_;
    std::unique_ptr<djehuti::animation::LocomotionController> procAnim_;
    std::string currentCharClip_;
    float characterYaw_ = 0.0f; // Smoothed facing direction
    std::unique_ptr<PhysicsWorld> physics_;
    
    enum class CameraMode { ThirdPerson, FirstPerson, TopDown };
    // ThirdPerson by default - nothing currently lets the player switch
    // modes at runtime, and FirstPerson hides the character entirely,
    // which would make the whole animated-character feature invisible.
    CameraMode cameraMode_ = CameraMode::ThirdPerson;
};
}

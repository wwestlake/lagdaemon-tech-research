#include "OpenWorld.h"

namespace Harmonia {
OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, juce::OpenGLContext* ctx)
    : worldState_(state), audio_(audio), net_(net) {
    ground_ = std::make_unique<GroundPlane>();
    
    playerMesh_ = std::make_unique<Mesh>();
    modelRenderer_ = std::make_unique<ModelRenderer>();
    
    // Determine path to project root
    juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File root = exe.getParentDirectory().getParentDirectory();
    juce::File fbx = root.getChildFile("female.fbx");
    if (!fbx.existsAsFile()) fbx = root.getChildFile("female.FBX");
    if (!fbx.existsAsFile()) fbx = root.getChildFile("David_Idle.FBX");
    
    if (fbx.existsAsFile()) {
        playerMesh_->loadFBX(fbx.getFullPathName().toStdString());
    } else {
        juce::Logger::writeToLog("Could not find FBX at: " + root.getFullPathName());
    }
}

void OpenWorld::update(float dt, const std::set<int>& keysDown, float mouseDx, float mouseDy) {
    localPlayer_.mouseMove(mouseDx, mouseDy);
    localPlayer_.update(dt, keysDown);
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx) {
    ground_->render(view, proj, ctx);
    
    // Lazy init mesh GL buffers
    static bool meshSetup = false;
    if (!meshSetup && playerMesh_) {
        playerMesh_->setupGL(ctx);
        meshSetup = true;
    }
    
    if (playerMesh_ && modelRenderer_) {
        // Draw player at origin (scaled down if UE4 uses cm)
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::scale(model, glm::vec3(0.01f)); // UE4 is cm, OpenGL is m usually
        modelRenderer_->render(*playerMesh_, model, view, proj, ctx);
    }
    
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

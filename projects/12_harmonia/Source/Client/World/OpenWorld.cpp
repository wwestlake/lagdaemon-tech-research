#include "OpenWorld.h"

namespace Harmonia {
OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, Camera& camera)
    : worldState_(state), audio_(audio), net_(net), camera_(camera) {
    ground_ = std::make_unique<GroundPlane>();
    playerChar_ = std::make_unique<BlockCharacter>();
    camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
}

void OpenWorld::update(float dt, float mouseDx, float mouseDy) {
    localPlayer_.mouseMove(mouseDx, mouseDy);
    
    glm::vec3 oldPos = localPlayer_.position();
    localPlayer_.update(dt, camera_.azimuth);
    glm::vec3 velocity = (localPlayer_.position() - oldPos) / dt;
    
    if (playerChar_) {
        playerChar_->update(dt, velocity);
    }
    
    if (cameraMode_ == CameraMode::FirstPerson) {
        camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
    } else {
        camera_.setPivot(localPlayer_.position());
    }
    
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx) {
    ground_->render(view, proj, ctx);
    
    // Lazy init mesh GL buffers
    static bool charSetup = false;
    if (!charSetup && playerChar_) {
        playerChar_->initialise(ctx);
        charSetup = true;
    }
    
    if (playerChar_) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, localPlayer_.position());
        model = glm::rotate(model, camera_.azimuth, glm::vec3(0, 1, 0));
        
        // Use the existing solid colour shader if we have one, or create a simple one.
        // Wait, BlockCharacter expects a shader passed in! 
        // We need a basic shader.
        // Let's create one inline just for the character.
        static std::unique_ptr<juce::OpenGLShaderProgram> charShader;
        if (!charShader) {
            charShader = std::make_unique<juce::OpenGLShaderProgram>(ctx);
            const char* vsh = R"(
                #version 330 core
                layout(location=0) in vec3 aPos;
                uniform mat4 uVP;
                uniform mat4 uModel;
                void main() {
                    gl_Position = uVP * uModel * vec4(aPos, 1.0);
                }
            )";
            const char* fsh = R"(
                #version 330 core
                out vec4 fragColor;
                uniform vec4 uColor;
                void main() {
                    fragColor = uColor;
                }
            )";
            charShader->addVertexShader(vsh);
            charShader->addFragmentShader(fsh);
            charShader->link();
        }
        
        if (cameraMode_ != CameraMode::FirstPerson) {
            playerChar_->render(*charShader, view, proj, model, camera_.position());
        }
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
} // namespace Harmonia

#include "OpenWorld.h"
#include <iostream>
#include <cmath>

namespace Harmonia {
OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, Camera& camera)
    : worldState_(state), audio_(audio), net_(net), camera_(camera) {
    ground_ = std::make_unique<GroundPlane>();

    playerChar_ = std::make_unique<GltfCharacter>();
    // Dev-build asset resolution: Builds/Harmonia_artefacts/Debug/Harmonia.exe
    // -> up 3 to the project root -> Characters/Human.gltf. Not meant to
    // survive a packaged release, fine for this research build.
    juce::File exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File projectRoot = exeFile.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
    juce::File charFile = projectRoot.getChildFile("Characters").getChildFile("Human.gltf");
    if (playerChar_->load(charFile)) {
        playerChar_->playClip("Idle");
    } else {
        std::cerr << "Harmonia: failed to load character from " << charFile.getFullPathName() << "\n";
    }

    physics_ = std::make_unique<PhysicsWorld>();
    physics_->initialise(localPlayer_.position());
    localPlayer_.setPosition(physics_->characterPosition());

    camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
}

namespace {
// Camera::position()'s own orbit formula places the camera at world
// offset (cos(az), sin(el), sin(az))*distance from the pivot and looks
// back AT the pivot - so its horizontal look direction (camera->pivot)
// sits at angle (az + pi) in that same (cos, sin) parameterization.
// PlayerController's forward-vector formula uses a DIFFERENT, 90-degree-
// rotated parameterization ((-sin, cos) instead of (cos, sin)), so
// feeding it the SAME azimuth value directly produces a forward vector
// 90 degrees off from where the camera is actually looking - which is
// exactly why the camera was sitting beside the character instead of
// behind it. Adding pi/2 here corrects the phase so "forward" (movement)
// and the camera's real look direction agree. Derived directly from
// both formulas, not guessed.
constexpr float kAzimuthToFacing = 1.57079633f; // pi/2
}

void OpenWorld::update(float dt) {
    glm::vec3 oldPos = localPlayer_.position();

    glm::vec3 moveDir = localPlayer_.computeMoveDir(camera_.azimuth + kAzimuthToFacing);
    if (physics_) {
        physics_->update(dt, moveDir, localPlayer_.jumpHeld());
        localPlayer_.setPosition(physics_->characterPosition());
    }
    glm::vec3 velocity = (localPlayer_.position() - oldPos) / dt;

    // Minecraft-style scheme: mouse look sets the facing continuously
    // (camera_.azimuth, driven by real mouse deltas - see
    // HarmoniaGLContext::renderOpenGL), the character always matches
    // that same facing, and W moves in that direction. The camera trails
    // the player's POSITION only; it does not derive its own orientation
    // from movement the way an over-the-shoulder "chase cam" would.
    bool moving = glm::length(glm::vec2(velocity.x, velocity.z)) > 0.1f;

    if (playerChar_) {
        const char* wantClip = moving ? "Walk" : "Idle";
        if (currentCharClip_ != wantClip) {
            playerChar_->playClip(wantClip);
            currentCharClip_ = wantClip;
        }
        playerChar_->update(dt);
    }

    if (cameraMode_ == CameraMode::FirstPerson) {
        camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
    } else {
        // Look at roughly chest/head height, not the character's feet
        // (localPlayer_.position() is ground level, since the character
        // is grounded there) - otherwise the camera ends up staring at
        // its knees.
        camera_.setPivotTarget(localPlayer_.position() + glm::vec3(0.f, 1.4f, 0.f));
    }
    
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                        const glm::vec3& sunDir, const glm::vec3& sunColor) {
    ground_->render(view, proj, ctx, sunDir, sunColor);

    if (playerChar_ && cameraMode_ != CameraMode::FirstPerson) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, localPlayer_.position());
        // The character always faces the same direction the mouse-look
        // camera is aimed (industry-standard third-person: look and
        // heading are the same thing) - same kAzimuthToFacing correction
        // as movement above, so the model's facing actually matches the
        // direction W walks in and the direction the camera looks.
        model = glm::rotate(model, -(camera_.azimuth + kAzimuthToFacing), glm::vec3(0, 1, 0));

        // A rich, deep "polished car paint" blue - stylistic placeholder
        // colour, no texture yet. The shine itself (specular + fresnel)
        // comes from GltfCharacter's own shader, not from this value.
        const glm::vec3 kCharacterBlue(0.05f, 0.22f, 0.55f);
        playerChar_->render(ctx, view, proj, model, sunDir, sunColor, kCharacterBlue);
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

#include "OpenWorld.h"
#include "Client/Engine/Rendering/TerrainHeight.h"
#include "Client/Engine/Rendering/BakedTerrain.h"
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iostream>

#define ANIM_TEST_MODE 1

#include "Client/Engine/Animation/IKSolver.h"
#include "Client/Engine/Animation/LocomotionController.h"

namespace Harmonia {
extern bool g_MouseCaptured;

OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, Camera& camera)
    : worldState_(state), audio_(audio), net_(net), camera_(camera) {
    
    juce::File exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File projectRoot = exeFile.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
    juce::File bakedFile = projectRoot.getChildFile("baked_terrain.dat");
    
    if (!BakedTerrain::get().load(bakedFile.getFullPathName().toStdString())) {
        std::cout << "Baked terrain not found, generating...\n";
        BakedTerrain::get().bakeAndSave(bakedFile.getFullPathName().toStdString(), -1024.0f, -1024.0f, 1024.0f, 1024.0f, 1.0f);
    }
    
    ground_ = std::make_unique<Terrain>();

    playerChar_ = std::make_unique<GltfCharacter>();
    // Dev-build asset resolution: Builds/Harmonia_artefacts/Debug/Harmonia.exe
    // -> up 3 to the project root -> Characters/Human.gltf. Not meant to
    // survive a packaged release, fine for this research build.
    juce::File charFile = projectRoot.getChildFile("Characters").getChildFile("EditorMainFemale.gltf");
    if (playerChar_->load(charFile)) {
        playerChar_->playClip("Idle");
    } else {
        std::cerr << "Harmonia: failed to load character from " << charFile.getFullPathName() << "\n";
    }

    physics_ = std::make_unique<PhysicsWorld>();
    // PlayerController::pos_ defaults to a hardcoded {0, 2, 0} - a
    // leftover from before real physics existed, when height got
    // force-snapped every frame regardless of the starting value. Query
    // the REAL terrain height at the spawn XZ instead, or the capsule
    // spawns floating (or embedded) relative to actual ground and free-
    // falls/settles awkwardly before physics catches up.
    glm::vec3 spawnPos(0.0f, BakedTerrain::get().heightAt(0.0f, 0.0f) + 1.0f, 0.0f);
    physics_->initialise(spawnPos);
    localPlayer_.setPosition(physics_->characterPosition());

    procAnim_ = std::make_unique<djehuti::animation::LocomotionController>();
    procAnim_->init(physics_->characterPosition());

    camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
}

OpenWorld::~OpenWorld() = default;

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
#if ANIM_TEST_MODE
    glm::vec3 moveDir = localPlayer_.computeMoveDir(camera_.azimuth + kAzimuthToFacing, Harmonia::g_MouseCaptured);
    bool moving = glm::length(glm::vec2(moveDir.x, moveDir.z)) > 0.01f;
    
    if (moving) {
        float targetYaw = std::atan2(moveDir.x, moveDir.z);
        float diff = targetYaw - characterYaw_;
        while (diff < -3.14159265f) diff += 2.0f * 3.14159265f;
        while (diff > 3.14159265f) diff -= 2.0f * 3.14159265f;
        float alpha = 1.0f - std::exp(-10.0f * dt);
        characterYaw_ += diff * alpha;
    }

    if (playerChar_) {
        const char* wantClip = moving ? "Walk" : "Idle";
        if (currentCharClip_ != wantClip) {
            playerChar_->playClip(wantClip);
            currentCharClip_ = wantClip;
        }

        glm::vec3 velocity = moving ? (moveDir * 4.0f) : glm::vec3(0.0f);
        if (localPlayer_.runHeld(g_MouseCaptured)) velocity *= 2.0f;
        
        if (procAnim_ && moving) {
            procAnim_->setTreadmillMode(true);
            procAnim_->update(glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), velocity, characterYaw_, dt, nullptr, nullptr);
            
            float thighLen = 0.45f;
            float calfLen = 0.45f;
            float maxReach = thighLen + calfLen - 0.01f;
            
            glm::vec3 lTargetW = procAnim_->getLeftFootTarget();
            glm::vec3 rTargetW = procAnim_->getRightFootTarget();
            
            glm::vec3 nominalHipL = glm::vec3(-0.1f, 0.9f - procAnim_->getPelvisDrop(), 0.0f);
            glm::vec3 nominalHipR = glm::vec3( 0.1f, 0.9f - procAnim_->getPelvisDrop(), 0.0f);
            
            float dL = glm::length(lTargetW - nominalHipL);
            float dR = glm::length(rTargetW - nominalHipR);
            
            float extraDrop = 0.0f;
            if (dL > maxReach) extraDrop = std::max(extraDrop, dL - maxReach);
            if (dR > maxReach) extraDrop = std::max(extraDrop, dR - maxReach);
            
            glm::vec3 worldRootPos = glm::vec3(0, 0.9f - procAnim_->getPelvisDrop() - extraDrop, 0);
            
            glm::mat4 model(1.0f);
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            model = glm::rotate(model, characterYaw_, glm::vec3(0, 0, 1));
            glm::mat4 invModel = glm::inverse(model);

            glm::vec3 defaultBoneDir(0, 0, -1);
            
            int tL = playerChar_->findNodeIndex("thigh_l");
            int cL = playerChar_->findNodeIndex("calf_l");
            if (tL >= 0 && cL >= 0) {
                glm::vec3 localRoot = glm::vec3(invModel * glm::vec4(worldRootPos + glm::vec3(-0.1f, 0, 0), 1.0f));
                glm::vec3 localTarget = glm::vec3(invModel * glm::vec4(lTargetW, 1.0f));
                auto [hipL, kneeL] = djehuti::animation::IKSolver::solveTwoBoneIK(
                    localRoot, localTarget, glm::vec3(0, -1, 0), thighLen, calfLen, defaultBoneDir
                );
                playerChar_->setNodeOverride(tL, hipL);
                playerChar_->setNodeOverride(cL, kneeL);
            }
            
            int tR = playerChar_->findNodeIndex("thigh_r");
            int cR = playerChar_->findNodeIndex("calf_r");
            if (tR >= 0 && cR >= 0) {
                glm::vec3 localRoot = glm::vec3(invModel * glm::vec4(worldRootPos + glm::vec3(0.1f, 0, 0), 1.0f));
                glm::vec3 localTarget = glm::vec3(invModel * glm::vec4(rTargetW, 1.0f));
                auto [hipR, kneeR] = djehuti::animation::IKSolver::solveTwoBoneIK(
                    localRoot, localTarget, glm::vec3(0, -1, 0), thighLen, calfLen, defaultBoneDir
                );
                playerChar_->setNodeOverride(tR, hipR);
                playerChar_->setNodeOverride(cR, kneeR);
            }
            
            playerChar_->applyOverrides();
        } else {
            playerChar_->clearOverrides();
            if (procAnim_) {
                procAnim_->update(glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), glm::vec3(0), characterYaw_, dt, nullptr, nullptr);
            }
        }
        playerChar_->update(dt);
    }
#else
    glm::vec3 oldPos = localPlayer_.position();    // Read input using camera direction
    glm::vec3 moveDir = localPlayer_.computeMoveDir(camera_.azimuth, Harmonia::g_MouseCaptured);
    bool doJump = localPlayer_.jumpHeld(Harmonia::g_MouseCaptured);
    bool doRun = localPlayer_.runHeld(Harmonia::g_MouseCaptured);
    if (physics_) {
        physics_->update(dt, moveDir, doJump, doRun);
        localPlayer_.setPosition(physics_->characterPosition());
        
        constexpr float kVoidY = -50.0f;
        if (localPlayer_.position().y < kVoidY) {
            glm::vec3 respawnPos(0.0f, BakedTerrain::get().heightAt(0.0f, 0.0f) + 1.0f, 0.0f);
            physics_->teleportCharacter(respawnPos);
            localPlayer_.setPosition(physics_->characterPosition());
        }
    }
    
    glm::vec3 velocity = (localPlayer_.position() - oldPos) / dt;

    // Minecraft-style scheme: mouse look sets the facing continuously
    // (camera_.azimuth, driven by real mouse deltas - see
    // HarmoniaGLContext::renderOpenGL), the character always matches
    // that same facing, and W moves in that direction. The camera trails
    // the player's POSITION only; it does not derive its own orientation
    // from movement the way an over-the-shoulder "chase cam" would.
    bool moving = glm::length(glm::vec2(moveDir.x, moveDir.z)) > 0.01f;
    
    if (moving) {
        float targetYaw = std::atan2(moveDir.x, moveDir.z);
        
        // Smoothly interpolate yaw (shortest path) to avoid overshoot
        float diff = targetYaw - characterYaw_;
        while (diff < -3.14159265f) diff += 2.0f * 3.14159265f;
        while (diff > 3.14159265f) diff -= 2.0f * 3.14159265f;
        
        float alpha = 1.0f - std::exp(-10.0f * dt);
        characterYaw_ += diff * alpha;
    }

    if (playerChar_) {
        float speed = glm::length(glm::vec2(velocity.x, velocity.z));
        
        if (procAnim_ && moving) {
            procAnim_->update(localPlayer_.position() + glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), velocity, characterYaw_, dt, nullptr, nullptr);
            
            // Bone lengths for MakeHuman legs
            float thighLen = 0.45f;
            float calfLen = 0.45f;
            float maxReach = thighLen + calfLen - 0.01f;
            
            glm::vec3 lTargetW = procAnim_->getLeftFootTarget();
            glm::vec3 rTargetW = procAnim_->getRightFootTarget();
            
            glm::vec3 nominalHipL = localPlayer_.position() + glm::vec3(-0.1f, 0.9f - procAnim_->getPelvisDrop(), 0.0f);
            glm::vec3 nominalHipR = localPlayer_.position() + glm::vec3( 0.1f, 0.9f - procAnim_->getPelvisDrop(), 0.0f);
            
            float dL = glm::length(lTargetW - nominalHipL);
            float dR = glm::length(rTargetW - nominalHipR);
            
            float extraDrop = 0.0f;
            if (dL > maxReach) extraDrop = std::max(extraDrop, dL - maxReach);
            if (dR > maxReach) extraDrop = std::max(extraDrop, dR - maxReach);
            
            glm::vec3 worldRootPos = localPlayer_.position() + glm::vec3(0, 0.9f - procAnim_->getPelvisDrop() - extraDrop, 0);
            
            glm::mat4 model(1.0f);
            model = glm::translate(model, localPlayer_.position());
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            model = glm::rotate(model, characterYaw_, glm::vec3(0, 0, 1));
            glm::mat4 invModel = glm::inverse(model);

            glm::vec3 defaultBoneDir(0, 0, -1);
            
            int tL = playerChar_->findNodeIndex("thigh_l");
            int cL = playerChar_->findNodeIndex("calf_l");
            if (tL >= 0 && cL >= 0) {
                glm::vec3 localRoot = glm::vec3(invModel * glm::vec4(worldRootPos + glm::vec3(-0.1f, 0, 0), 1.0f));
                glm::vec3 localTarget = glm::vec3(invModel * glm::vec4(lTargetW, 1.0f));
                auto [hipL, kneeL] = djehuti::animation::IKSolver::solveTwoBoneIK(
                    localRoot, localTarget, glm::vec3(0, -1, 0), thighLen, calfLen, defaultBoneDir
                );
                playerChar_->setNodeOverride(tL, hipL);
                playerChar_->setNodeOverride(cL, kneeL);
            }
            
            int tR = playerChar_->findNodeIndex("thigh_r");
            int cR = playerChar_->findNodeIndex("calf_r");
            if (tR >= 0 && cR >= 0) {
                glm::vec3 localRoot = glm::vec3(invModel * glm::vec4(worldRootPos + glm::vec3(0.1f, 0, 0), 1.0f));
                glm::vec3 localTarget = glm::vec3(invModel * glm::vec4(rTargetW, 1.0f));
                auto [hipR, kneeR] = djehuti::animation::IKSolver::solveTwoBoneIK(
                    localRoot, localTarget, glm::vec3(0, -1, 0), thighLen, calfLen, defaultBoneDir
                );
                playerChar_->setNodeOverride(tR, hipR);
                playerChar_->setNodeOverride(cR, kneeR);
            }
            
            playerChar_->applyOverrides();
        } else {
            // When standing still, remove IK overrides so the baked "Idle" breathing clip plays naturally
            playerChar_->clearOverrides();
            if (procAnim_) {
                procAnim_->update(localPlayer_.position() + glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), glm::vec3(0), characterYaw_, dt, nullptr, nullptr);
            }
        }

        playerChar_->update(dt);
    }

    if (cameraMode_ == CameraMode::FirstPerson) {
        camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
    } else {
        // Look at head height so camera is above looking slightly down
        camera_.setPivotTarget(localPlayer_.position() + glm::vec3(0.f, 1.7f, 0.f));
    }
#endif
    
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx,
                        const glm::vec3& sunDir, const glm::vec3& sunColor) {
    glm::vec3 camPos = camera_.position();
    ground_->update(ctx, camPos);
    ground_->render(view, proj, ctx, sunDir, sunColor, camPos);

    if (playerChar_) {
        // Place character at physics position's bottom (which is ground level)
        glm::mat4 model(1.0f);
        model = glm::translate(model, localPlayer_.position());
        
        // Rotate to match the smoothed character yaw
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        model = glm::rotate(model, characterYaw_, glm::vec3(0, 0, 1));
        


        // A rich, deep "polished car paint" blue - stylistic placeholder
        // colour, no texture yet. The shine itself (specular + fresnel)
        // comes from GltfCharacter's own shader, not from this value.
        const glm::vec3 kCharacterBlue(0.05f, 0.22f, 0.55f);
        playerChar_->render(ctx, playerChar_->getSkinnedVertexData(), view, proj, model, sunDir, sunColor, kCharacterBlue);
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

void OpenWorld::toggleFirstPerson() {
    cameraMode_ = (cameraMode_ == CameraMode::FirstPerson) ? CameraMode::ThirdPerson : CameraMode::FirstPerson;
}

void OpenWorld::detectRegion() {}
} // namespace Harmonia

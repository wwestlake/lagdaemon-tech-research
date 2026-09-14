#include "AnimTestWorld.h"
#include "Client/Engine/Animation/IKSolver.h"
#include <juce_core/juce_core.h>
#include <glm/gtc/matrix_transform.hpp>
#include "Client/Engine/Animation/LocomotionController.h"

using namespace juce::gl;

namespace Harmonia {
extern bool g_MouseCaptured;

AnimTestWorld::AnimTestWorld(Camera& camera) : camera_(camera) {
    playerChar_ = std::make_unique<GltfCharacter>();
    juce::File projectRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
    juce::File charFile = projectRoot.getChildFile("Characters").getChildFile("EditorMainFemale.gltf");
    
    if (playerChar_->load(charFile)) {
        playerChar_->playClip("Idle");
    }
    
    procAnim_ = std::make_unique<djehuti::animation::LocomotionController>();
    procAnim_->init(glm::vec3(0,0,0));
    procAnim_->setTreadmillMode(true);
    
    physics_ = std::make_unique<PhysicsWorld>();
    glm::vec3 spawnPos(0.0f, 1.0f, -5.0f); // Spawn local player at -5m so +Z looks at character
    physics_->initialise(spawnPos, true); // useFlatFloor = true
    localPlayer_.setPosition(physics_->characterPosition());
    
    // Set camera to look down +Z (towards character at 0,0,0)
    camera_.setOrientation(0.0f, 0.0f, 0.0f);
    camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
}

AnimTestWorld::~AnimTestWorld() = default;

void AnimTestWorld::update(float dt) {
    // T key to toggle animation state of the dummy character
    bool tDown = juce::KeyPress::isKeyCurrentlyDown('T') || juce::KeyPress::isKeyCurrentlyDown('t');
    if (tDown && !wWasDown_) {
        walking_ = !walking_;
    }
    wWasDown_ = tDown;
    
    float speed = walking_ ? 1.5f : 0.0f; // 1.5 m/s walk speed
    
    // Process local player FPS movement
    // We only process input if the mouse is currently captured (window is active and interacting)
    glm::vec3 moveDir = localPlayer_.computeMoveDir(camera_.azimuth, Harmonia::g_MouseCaptured);
    
    if (physics_) {
        physics_->update(dt, moveDir, localPlayer_.jumpHeld(Harmonia::g_MouseCaptured), localPlayer_.runHeld(Harmonia::g_MouseCaptured));
        localPlayer_.setPosition(physics_->characterPosition());
        
        // Anti-fall through safety net
        if (localPlayer_.position().y < -10.0f) {
            physics_->teleportCharacter(glm::vec3(0.0f, 1.0f, -5.0f));
            localPlayer_.setPosition(physics_->characterPosition());
        }
    }
    
    camera_.setFirstPersonPosition(localPlayer_.position() + glm::vec3(0.f, 1.6f, 0.f));
    camera_.update(dt);

    if (playerChar_) {
        bool moving = speed > 0.1f;
        const char* wantClip = moving ? "Walk" : "Idle";
        if (currentClip_ != wantClip) {
            playerChar_->playClip(wantClip);
            currentClip_ = wantClip;
        }
        
        if (moving) {
            glm::vec3 forward(-std::sin(characterYaw_), 0.0f, std::cos(characterYaw_));
            glm::vec3 velocity = forward * speed;
            procAnim_->update(glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), velocity, characterYaw_, dt, nullptr, nullptr);
            
            float thighLen = 0.45f;
            float calfLen = 0.45f;
            float maxReach = thighLen + calfLen - 0.01f;
            
            glm::vec3 lTargetW = procAnim_->getLeftFootTarget();
            glm::vec3 rTargetW = procAnim_->getRightFootTarget();
            
            // 1. Singular Reach Guard & Pelvis Drop
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
            procAnim_->update(glm::vec3(0, 0.9f - procAnim_->getPelvisDrop(), 0), glm::vec3(0,0,0), characterYaw_, dt, nullptr, nullptr);
        }
    playerChar_->update(dt);
        
        // Write evaluated state into the triple buffer
        auto& writeState = renderStateBuffer_.getWriteBuffer();
        writeState.cameraView = camera_.viewMatrix();
        writeState.cameraProj = camera_.projectionMatrix(16.0f / 9.0f); // Default to 16:9 in update thread, render will pass its own if possible or we can compute in render. Wait, it's better to pass the Camera's azimuth/elevation/pos and let render compute matrices?
        writeState.cameraPos = camera_.position();
        
        // We actually only need the final bone transforms
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        model = glm::rotate(model, characterYaw_, glm::vec3(0, 0, 1));
        writeState.charTransform = model;
        
        // Ensure GltfCharacter has a way to get bone transforms or just update the matrices.
        // GltfCharacter's skinning is handled internally in render(), but if we thread it, 
        // we should copy the pose. 
        // Wait! GltfCharacter::render uses its internal `globalTransforms_`. 
        // If we want thread safety without exposing internal vectors, we can just say:
        // Actually, this is a prototype, I will just give it a public method or friend it, 
        // or just let GltfCharacter be single-buffered for now?
        // NO, the user wants triple buffering. I will get globalTransforms from GltfCharacter.
        writeState.charVertexData = playerChar_->getSkinnedVertexData();
        
        renderStateBuffer_.commitWrite();
    }
}

void AnimTestWorld::render(float aspectRatio, juce::OpenGLContext& ctx, const glm::vec3& sunDir, const glm::vec3& sunColor) {
    renderStateBuffer_.updateReadBuffer();
    const auto& state = renderStateBuffer_.getReadBuffer();
    
    glm::mat4 view = state.cameraView;
    glm::mat4 proj = camera_.projectionMatrix(aspectRatio); // Camera is thread-safe enough for projection, or we just calculate it here
    
    if (!labShader_) {
        labShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
        labShader_->addVertexShader(R"(
            #version 330 core
            layout(location=0) in vec3 aPos;
            layout(location=1) in vec3 aCol;
            uniform mat4 mvp;
            out vec3 vCol;
            void main() {
                gl_Position = mvp * vec4(aPos, 1.0);
                vCol = aCol;
            }
        )");
        labShader_->addFragmentShader(R"(
            #version 330 core
            in vec3 vCol;
            out vec4 FragColor;
            void main() {
                FragColor = vec4(vCol, 1.0);
            }
        )");
        labShader_->link();
        
        ctx.extensions.glGenVertexArrays(1, &vao_);
        ctx.extensions.glBindVertexArray(vao_);
        ctx.extensions.glGenBuffers(1, &vbo_);
        ctx.extensions.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        
        std::vector<float> verts;
        auto addQuad = [&](glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 p4, glm::vec3 col) {
            verts.insert(verts.end(), {p1.x, p1.y, p1.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p2.x, p2.y, p2.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p3.x, p3.y, p3.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p1.x, p1.y, p1.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p3.x, p3.y, p3.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p4.x, p4.y, p4.z, col.r, col.g, col.b});
        };
        
        addQuad({-15, 0, -15}, {15, 0, -15}, {15, 0, 15}, {-15, 0, 15}, {0.85f, 0.85f, 0.85f});
        addQuad({-15, 10, 15}, {15, 10, 15}, {15, 10, -15}, {-15, 10, -15}, {1.0f, 1.0f, 1.0f});
        addQuad({-15, 0, -15}, {-15, 10, -15}, {15, 10, -15}, {15, 0, -15}, {0.7f, 0.7f, 0.7f});
        addQuad({15, 0, 15}, {15, 10, 15}, {-15, 10, 15}, {-15, 0, 15}, {0.85f, 0.85f, 0.85f});
        addQuad({-15, 0, 15}, {-15, 10, 15}, {-15, 10, -15}, {-15, 0, -15}, {0.75f, 0.75f, 0.75f});
        addQuad({15, 0, -15}, {15, 10, -15}, {15, 10, 15}, {15, 0, 15}, {0.8f, 0.8f, 0.8f});
        
        auto addPad = [&](float cx, float cz, float radius) {
            glm::vec3 col(0.2f, 0.2f, 0.2f);
            glm::vec3 center(cx, 0.02f, cz);
            for (int i = 0; i < 32; ++i) {
                float a1 = i * 2.0f * 3.14159f / 32.0f;
                float a2 = (i + 1) * 2.0f * 3.14159f / 32.0f;
                verts.insert(verts.end(), {center.x, center.y, center.z, col.r, col.g, col.b});
                verts.insert(verts.end(), {cx + std::cos(a1)*radius, 0.02f, cz + std::sin(a1)*radius, col.r, col.g, col.b});
                verts.insert(verts.end(), {cx + std::cos(a2)*radius, 0.02f, cz + std::sin(a2)*radius, col.r, col.g, col.b});
            }
        };
        addPad(0, 0, 1.5f); addPad(5, 5, 1.5f); addPad(-5, -5, 1.5f); addPad(-5, 5, 1.5f); addPad(5, -5, 1.5f);
        
        numTriVerts_ = (int)(verts.size() / 6);
        
        auto addLine = [&](glm::vec3 p1, glm::vec3 p2, glm::vec3 col) {
            verts.insert(verts.end(), {p1.x, p1.y, p1.z, col.r, col.g, col.b});
            verts.insert(verts.end(), {p2.x, p2.y, p2.z, col.r, col.g, col.b});
        };
        
        for (int i = -150; i <= 150; ++i) {
            float v = i * 0.1f;
            glm::vec3 col = (i % 10 == 0) ? glm::vec3(0.5f) : glm::vec3(0.8f);
            addLine({v, 0.01f, -15}, {v, 0.01f, 15}, col);
            addLine({v, 9.99f, -15}, {v, 9.99f, 15}, col);
            addLine({-15, 0.01f, v}, {15, 0.01f, v}, col);
            addLine({-15, 9.99f, v}, {15, 9.99f, v}, col);
        }
        for (int i = 0; i <= 100; ++i) {
            float y = i * 0.1f;
            glm::vec3 col = (i % 10 == 0) ? glm::vec3(0.5f) : glm::vec3(0.8f);
            addLine({-14.99f, y, -15}, {-14.99f, y, 15}, col);
            addLine({14.99f, y, -15}, {14.99f, y, 15}, col);
            addLine({-15, y, -14.99f}, {15, y, -14.99f}, col);
            addLine({-15, y, 14.99f}, {15, y, 14.99f}, col);
        }
        
        numLineVerts_ = (int)(verts.size() / 6) - numTriVerts_;
        
        ctx.extensions.glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        ctx.extensions.glEnableVertexAttribArray(0);
        ctx.extensions.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        ctx.extensions.glEnableVertexAttribArray(1);
        ctx.extensions.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    
    if (labShader_) {
        labShader_->use();
        glm::mat4 mvp = proj * view;
        GLint mvpLoc = labShader_->getUniformIDFromName("mvp");
        if (mvpLoc >= 0) {
            ctx.extensions.glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        }
        
        ctx.extensions.glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, numTriVerts_);
        glDrawArrays(GL_LINES, numTriVerts_, numLineVerts_);
        ctx.extensions.glBindVertexArray(0);
    }

    if (playerChar_ && !state.charVertexData.empty()) {
        glm::vec3 labSunDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f));
        glm::vec3 labSunColor(1.2f, 1.2f, 1.2f);
        glm::vec3 labAmbient(0.4f, 0.4f, 0.4f);
        
        playerChar_->render(ctx, state.charVertexData, view, proj, state.charTransform, labSunDir, labSunColor, labAmbient);
    }
}

void AnimTestWorld::onClick() {
    glm::vec3 ro = camera_.position();
    glm::vec3 rd = camera_.forward();
    glm::vec2 p = glm::vec2(ro.x, ro.z);
    glm::vec2 d = glm::vec2(rd.x, rd.z);
    float a = glm::dot(d, d);
    float b = 2.0f * glm::dot(p, d);
    float c = glm::dot(p, p) - 0.25f;
    if (a > 0.0001f) {
        float disc = b * b - 4.0f * a * c;
        if (disc > 0.0f) {
            float t1 = (-b - std::sqrt(disc)) / (2.0f * a);
            float t2 = (-b + std::sqrt(disc)) / (2.0f * a);
            float t = (t1 > 0.0f) ? t1 : ((t2 > 0.0f) ? t2 : -1.0f);
            if (t > 0.0f) {
                float y = ro.y + t * rd.y;
                if (y >= 0.0f && y <= 2.0f) {
                    walking_ = !walking_;
                }
            }
        }
    }
}

} // namespace Harmonia

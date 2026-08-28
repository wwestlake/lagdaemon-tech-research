#include "OpenGLContext.h"
#include "VoxelRenderer.h"
#include "Client/World/OpenWorld.h"
#include <glm/gtc/type_ptr.hpp>

using namespace juce::gl;

namespace Harmonia {

HarmoniaGLContext::HarmoniaGLContext() {
    glCtx_.setRenderer(this);
    glCtx_.setContinuousRepainting(true);
    glCtx_.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
}

HarmoniaGLContext::~HarmoniaGLContext() {
    detach();
}

void HarmoniaGLContext::attachTo(juce::Component& comp) {
    glCtx_.attachTo(comp);
    attachedComponent_ = &comp;
}

void HarmoniaGLContext::detach() {
    glCtx_.detach();
    attachedComponent_ = nullptr;
}

// ─── Called once when GL context is created (on the GL thread) ───────────────
void HarmoniaGLContext::newOpenGLContextCreated() {
    shaders_ = std::make_unique<ShaderLibrary>();
    shaders_->initialise(glCtx_);

    voxelRenderer_ = std::make_unique<VoxelRenderer>();
    voxelRenderer_->initialise(glCtx_);

    particles_ = std::make_unique<ParticleSystem>();
    particles_->initialise(glCtx_);

    stars_ = std::make_unique<StarField>();
    stars_->initialise(glCtx_, 2000);

    camera_.setOrientation(0.4f, 0.35f, 55.f);
}

// ─── Main render callback — called 60Hz on the GL thread ─────────────────────
void HarmoniaGLContext::renderOpenGL() {
    const float w = (float)glCtx_.getRenderingScale() *
                    (attachedComponent_ ? attachedComponent_->getWidth() : 800);
    const float h = (float)glCtx_.getRenderingScale() *
                    (attachedComponent_ ? attachedComponent_->getHeight() : 600);
    const float aspect = (h > 0.f) ? w / h : 1.f;

    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glClearColor(0.01f, 0.01f, 0.06f, 1.f);   // deep space blue-black
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float  dt  = (lastRenderTime_ > 0) ? (float)(now - lastRenderTime_) : 0.016f;
    lastRenderTime_ = now;
    time_ += dt;

    camera_.update(dt);
    particles_->update(dt);

    const glm::mat4 view = camera_.viewMatrix();
    const glm::mat4 proj = camera_.projectionMatrix(aspect);
    const glm::mat4 vp   = proj * view;
    const glm::vec3 camPos = camera_.position();

    // ── 1. Star field ────────────────────────────────────────────────────────
    if (shaders_->starfield()) {
        glDisable(GL_DEPTH_TEST);
        stars_->draw(*shaders_->starfield(), vp);
    }

    // ── 2. Voxel grid ────────────────────────────────────────────────────────
    glEnable(GL_DEPTH_TEST);
    if (shaders_->voxel() && voxelRenderer_) {
        // Upload updated instance data to GPU
        {
            juce::ScopedReadLock sl(gridLock_);
            if (pendingGrid_) {
                voxelRenderer_->update(*pendingGrid_);

                // Upload to GPU instance VBO
                glCtx_.extensions.glBindVertexArray(0); // unbind first
                // Re-bind and upload instance buffer
                // (VoxelRenderer's VAO is already set up; we just update the VBO data)
                if (voxelRenderer_->activeVoxelCount() > 0) {
                    // This upload happens in draw() context — bind the VAO
                }
            }
        }

        voxelRenderer_->draw(*shaders_->voxel(), vp, camPos, time_);
    }

    // ── 3. Particles ─────────────────────────────────────────────────────────    // 3. OpenWorld (Ground, etc.)
    if (openWorld_) {
        openWorld_->render(view, proj, glCtx_);
    }

    // 4. Particles
    if (particles_) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        particles_->render(view, proj);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }
}

void HarmoniaGLContext::openGLContextClosing() {
    if (shaders_)        shaders_->shutdown();
    if (particles_)      particles_->shutdown();
    if (stars_)          stars_->shutdown();
    voxelRenderer_.reset();
    shaders_.reset();
    particles_.reset();
    stars_.reset();
}

// ─── Called from network/world thread — thread-safe ──────────────────────────
void HarmoniaGLContext::setVoxelGrid(std::shared_ptr<VoxelGrid> grid) {
    juce::ScopedWriteLock sl(gridLock_);
    pendingGrid_ = grid;
}

void HarmoniaGLContext::setWorldState(WorldState* state) {
    worldState_ = state;
}

Camera& HarmoniaGLContext::camera() { return camera_; }

} // namespace Harmonia

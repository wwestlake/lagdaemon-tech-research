#pragma once
#include <juce_opengl/juce_opengl.h>
#include <memory>
#include "Camera.h"
#include "ShaderLibrary.h"
#include "ParticleSystem.h"
#include "StarField.h"
#include "Shared/World/VoxelGrid.h"
#include "Shared/World/WorldState.h"

namespace Harmonia {

class VoxelRenderer;

class HarmoniaGLContext : public juce::OpenGLRenderer {
public:
    HarmoniaGLContext();
    ~HarmoniaGLContext() override;

    void attachTo(juce::Component& comp);
    void detach();

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Thread-safe: called from network thread when new grid arrives
    void setVoxelGrid(std::shared_ptr<VoxelGrid> grid);
    void setWorldState(WorldState* state);
    void setOpenWorld(class OpenWorld* world) { openWorld_ = world; }

    Camera& camera();
    juce::OpenGLContext& glContext() { return glCtx_; }

private:
    juce::OpenGLContext glCtx_;
    juce::Component*    attachedComponent_ = nullptr;

    std::unique_ptr<ShaderLibrary>  shaders_;
    std::unique_ptr<VoxelRenderer>  voxelRenderer_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<StarField>      stars_;

    Camera      camera_;
    WorldState* worldState_ = nullptr;
    class OpenWorld* openWorld_ = nullptr;

    // Grid update double-buffer (network → GL thread)
    juce::ReadWriteLock          gridLock_;
    std::shared_ptr<VoxelGrid>   pendingGrid_;

    float  time_            = 0.f;
    double lastRenderTime_  = 0.0;
};

} // namespace Harmonia

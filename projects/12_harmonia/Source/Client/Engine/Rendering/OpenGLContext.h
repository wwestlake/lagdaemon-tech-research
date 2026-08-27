#pragma once
#include <juce_opengl/juce_opengl.h>
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
    
    void setVoxelGrid(std::shared_ptr<VoxelGrid> grid);
    void setWorldState(WorldState* state);
    Camera& camera();
    juce::OpenGLContext& glContext() { return glCtx_; }
    
private:
    juce::OpenGLContext glCtx_;
    std::unique_ptr<ShaderLibrary> shaders_;
    std::unique_ptr<VoxelRenderer> voxelRenderer_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<StarField> stars_;
    Camera camera_;
    float sweepZ_ = 0.f;
    double lastRenderTime_ = 0.0;
};
}

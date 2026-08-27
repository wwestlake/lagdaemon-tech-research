#include "OpenGLContext.h"
#include <juce_core/juce_core.h>

namespace Harmonia {
class VoxelRenderer {
public:
    void render(juce::OpenGLShaderProgram* shader, const glm::mat4& vp) {}
};

HarmoniaGLContext::HarmoniaGLContext() {
    glCtx_.setRenderer(this);
    glCtx_.setContinuousRepainting(true);
}

HarmoniaGLContext::~HarmoniaGLContext() {
    detach();
}

void HarmoniaGLContext::attachTo(juce::Component& comp) {
    glCtx_.attachTo(comp);
}

void HarmoniaGLContext::detach() {
    glCtx_.detach();
}

void HarmoniaGLContext::newOpenGLContextCreated() {
    shaders_ = std::make_unique<ShaderLibrary>();
    shaders_->initialise(glCtx_);
    
    voxelRenderer_ = std::make_unique<VoxelRenderer>();
    particles_ = std::make_unique<ParticleSystem>();
    particles_->initialise(glCtx_);
    
    stars_ = std::make_unique<StarField>();
    stars_->initialise(glCtx_, 2000);
}

void HarmoniaGLContext::renderOpenGL() {
    juce::OpenGLHelpers::clear(juce::Colour(0xff050510));
    
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float dt = lastRenderTime_ > 0 ? (float)(now - lastRenderTime_) : 0.016f;
    lastRenderTime_ = now;
    
    camera_.update(dt);
    particles_->update(dt);
    
    // Rendering logic placeholder
}

void HarmoniaGLContext::openGLContextClosing() {
    shaders_->shutdown();
    particles_->shutdown();
    stars_->shutdown();
}

void HarmoniaGLContext::setVoxelGrid(std::shared_ptr<VoxelGrid> grid) {}
void HarmoniaGLContext::setWorldState(WorldState* state) {}
Camera& HarmoniaGLContext::camera() { return camera_; }
}

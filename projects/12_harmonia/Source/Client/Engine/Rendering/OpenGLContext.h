#pragma once
#include <juce_opengl/juce_opengl.h>
#include <memory>
#include "Camera.h"
#include "ShaderLibrary.h"
#include "ParticleSystem.h"
#include "StarField.h"

namespace Harmonia {

class HarmoniaGLContext : public juce::OpenGLRenderer {
public:
    HarmoniaGLContext();
    ~HarmoniaGLContext() override;

    void attachTo(juce::Component& comp);
    void detach();

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void setOpenWorld(class OpenWorld* world) { openWorld_ = world; }

    Camera& camera();
    juce::OpenGLContext& glContext() { return glCtx_; }

private:
    juce::OpenGLContext glCtx_;
    juce::Component*    attachedComponent_ = nullptr;

    std::unique_ptr<ShaderLibrary>  shaders_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<StarField>      stars_;

    Camera      camera_;
    class OpenWorld* openWorld_ = nullptr;

    float  time_            = 0.f;
    double lastRenderTime_  = 0.0;
};

} // namespace Harmonia

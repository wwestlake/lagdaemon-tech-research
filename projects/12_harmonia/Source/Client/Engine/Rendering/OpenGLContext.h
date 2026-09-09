#pragma once
#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <memory>
#include "Camera.h"
#include "ShaderLibrary.h"
#include "ParticleSystem.h"
#include "StarField.h"
#include "Sun.h"

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

    // Thread-safe handoff for mouse-look: the JUCE message thread (where
    // mouse events fire) ONLY ever writes here via atomic add. Camera
    // itself is mutated exclusively on the GL thread, inside
    // renderOpenGL() below - never touched directly from a mouse
    // callback, since the two threads run concurrently and Camera's own
    // fields are plain (non-atomic) floats.
    void addMouseDelta(float dx, float dy) {
        pendingMouseDx_.fetch_add(dx, std::memory_order_relaxed);
        pendingMouseDy_.fetch_add(dy, std::memory_order_relaxed);
    }

private:
    juce::OpenGLContext glCtx_;
    juce::Component*    attachedComponent_ = nullptr;

    std::atomic<float> pendingMouseDx_{0.0f};
    std::atomic<float> pendingMouseDy_{0.0f};

    std::unique_ptr<ShaderLibrary>  shaders_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<StarField>      stars_;
    std::unique_ptr<Sun>            sun_;

    Camera      camera_;
    class OpenWorld* openWorld_ = nullptr;

    float  time_            = 0.f;
    double lastRenderTime_  = 0.0;
};

} // namespace Harmonia

#pragma once
#include <juce_opengl/juce_opengl.h>

namespace Harmonia {
class PostProcess {
public:
    void initialise(juce::OpenGLContext& ctx, int width, int height);
    void resize(int width, int height);
    void shutdown();
    
    void beginCapture();
    void endCapture();
    void applyBloom();
private:
    GLuint fbo_ = 0, colorTex_ = 0, pingTex_ = 0, pongTex_ = 0;
    GLuint quadVAO_ = 0, quadVBO_ = 0;
    juce::OpenGLShaderProgram* bloomShader_ = nullptr;
    int width_ = 0, height_ = 0;
};
}

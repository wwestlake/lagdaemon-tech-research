#pragma once
#include <juce_opengl/juce_opengl.h>
#include <memory>

namespace Harmonia {
class ShaderLibrary {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    
    juce::OpenGLShaderProgram* voxel() const { return voxelShader_.get(); }
    juce::OpenGLShaderProgram* particle() const { return particleShader_.get(); }
    juce::OpenGLShaderProgram* sweep() const { return sweepShader_.get(); }
    juce::OpenGLShaderProgram* avatar() const { return avatarShader_.get(); }
    juce::OpenGLShaderProgram* starfield() const { return starfieldShader_.get(); }
    juce::OpenGLShaderProgram* bloom() const { return bloomShader_.get(); }
    juce::OpenGLShaderProgram* ground() const { return groundShader_.get(); }
    
private:
    std::unique_ptr<juce::OpenGLShaderProgram> voxelShader_, particleShader_, sweepShader_, avatarShader_, starfieldShader_, bloomShader_, groundShader_;
};
}

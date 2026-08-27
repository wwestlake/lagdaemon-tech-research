#include "ShaderLibrary.h"

namespace Harmonia {
void ShaderLibrary::initialise(juce::OpenGLContext& ctx) {
    const char* vsVoxel = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec4 aInstancePosAndState;
        layout(location = 2) in vec4 aInstanceColor;
        out vec4 vColor;
        uniform mat4 viewProj;
        void main() {
            vColor = aInstanceColor;
            gl_Position = viewProj * vec4(aPos + aInstancePosAndState.xyz, 1.0);
        }
    )";
    const char* fsVoxel = R"(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() { FragColor = vColor; }
    )";
    
    voxelShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    voxelShader_->addVertexShader(vsVoxel);
    voxelShader_->addFragmentShader(fsVoxel);
    voxelShader_->link();
    
    // Create other shaders similarly (stubs for brevity)
    particleShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    particleShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    particleShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    particleShader_->link();
    
    sweepShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    sweepShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    sweepShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    sweepShader_->link();
    
    avatarShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    avatarShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    avatarShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    avatarShader_->link();

    starfieldShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    starfieldShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    starfieldShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    starfieldShader_->link();

    bloomShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    bloomShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    bloomShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    bloomShader_->link();

    groundShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    groundShader_->addVertexShader("#version 330 core\n void main() { gl_Position = vec4(0); }");
    groundShader_->addFragmentShader("#version 330 core\n out vec4 f; void main() { f = vec4(1); }");
    groundShader_->link();
}

void ShaderLibrary::shutdown() {
    voxelShader_.reset();
    particleShader_.reset();
    sweepShader_.reset();
    avatarShader_.reset();
    starfieldShader_.reset();
    bloomShader_.reset();
    groundShader_.reset();
}
}

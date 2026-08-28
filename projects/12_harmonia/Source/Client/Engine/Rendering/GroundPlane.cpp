#include "GroundPlane.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

GroundPlane::GroundPlane() {}

GroundPlane::~GroundPlane() {
}

void GroundPlane::init(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;

    // A large quad for the ground plane
    std::vector<float> vertices = {
        // x, y, z,   u, v
        -500.0f, 0.0f, -500.0f,   0.0f, 0.0f,
         500.0f, 0.0f, -500.0f,   100.0f, 0.0f,
         500.0f, 0.0f,  500.0f,   100.0f, 100.0f,
        
        -500.0f, 0.0f, -500.0f,   0.0f, 0.0f,
         500.0f, 0.0f,  500.0f,   100.0f, 100.0f,
        -500.0f, 0.0f,  500.0f,   0.0f, 100.0f
    };

    ext.glGenVertexArrays(1, &vao_);
    ext.glBindVertexArray(vao_);

    ext.glGenBuffers(1, &vbo_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    ext.glEnableVertexAttribArray(1);

    ext.glBindVertexArray(0);

    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUV;
        uniform mat4 view;
        uniform mat4 proj;
        out vec2 uv;
        out vec3 worldPos;
        void main() {
            uv = aUV;
            worldPos = aPos;
            gl_Position = proj * view * vec4(aPos, 1.0);
        }
    )";

    const char* fShader = R"(
        #version 330 core
        in vec2 uv;
        in vec3 worldPos;
        out vec4 FragColor;
        
        void main() {
            // Checkerboard pattern
            vec2 c = floor(uv);
            float checker = mod(c.x + c.y, 2.0);
            vec3 col = mix(vec3(0.1, 0.1, 0.15), vec3(0.15, 0.15, 0.2), checker);
            
            // Fog fade-out based on distance
            float dist = length(worldPos.xz);
            float alpha = smoothstep(300.0, 100.0, dist);
            
            FragColor = vec4(col, alpha);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();
}

void GroundPlane::render(const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx) {
    if (!vao_) init(ctx);
    if (!shader_) return;

    auto& ext = ctx.extensions;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    shader_->use();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(shader_->getProgramID(), "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(shader_->getProgramID(), "proj"), 1, GL_FALSE, glm::value_ptr(proj));

    ext.glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    ext.glBindVertexArray(0);
    
    glDisable(GL_BLEND);
}

}

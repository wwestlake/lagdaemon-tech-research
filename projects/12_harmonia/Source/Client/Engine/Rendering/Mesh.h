#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "ufbx.h"

namespace Harmonia {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    
    bool loadFBX(const std::string& path);
    void setupGL(juce::OpenGLContext& ctx);
    void draw(juce::OpenGLContext& ctx);

private:
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
};

class ModelRenderer {
public:
    ModelRenderer();
    ~ModelRenderer();
    
    void init(juce::OpenGLContext& ctx);
    void render(Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx);
    
private:
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
};

}

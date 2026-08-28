#include "Mesh.h"
#include <iostream>
#include <juce_core/juce_core.h>
#include <glm/gtc/type_ptr.hpp>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

Mesh::~Mesh() {
}

bool Mesh::loadFBX(const std::string& path) {
    ufbx_load_opts opts = {0};
    opts.target_axes = ufbx_axes_right_handed_y_up; // Standard OpenGL
    opts.target_unit_meters = 1.0f; // Scale everything to meters

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        juce::Logger::writeToLog("Failed to load FBX: " + juce::String(error.description.data));
        return false;
    }

    // Find the first mesh in the scene
    ufbx_mesh* mesh = nullptr;
    for (size_t i = 0; i < scene->meshes.count; ++i) {
        mesh = scene->meshes.data[i];
        if (mesh && mesh->num_faces > 0) break;
    }

    if (!mesh) {
        juce::Logger::writeToLog("No mesh found in FBX!");
        ufbx_free_scene(scene);
        return false;
    }

    juce::Logger::writeToLog("Loading FBX Mesh: " + juce::String(mesh->name.data) + " with " + juce::String((int)mesh->num_faces) + " faces.");

    size_t max_triangles = mesh->max_face_triangles * mesh->num_faces;
    std::vector<uint32_t> tri_indices;
    tri_indices.resize(max_triangles * 3);
    
    uint32_t num_tris = 0;
    for (size_t i = 0; i < mesh->num_faces; ++i) {
        ufbx_face face = mesh->faces.data[i];
        num_tris += ufbx_triangulate_face(tri_indices.data() + num_tris * 3, tri_indices.size() - num_tris * 3, mesh, face);
    }
    
    vertices_.clear();
    indices_.clear();
    
    // We will build a unified vertex buffer
    // To keep it simple, we'll just create a vertex for every triangle corner (unindexed)
    // or properly indexed if we map them. Let's just create a flat list of vertices for now.
    
    vertices_.resize(num_tris * 3);
    for (uint32_t i = 0; i < num_tris * 3; ++i) {
        uint32_t index = tri_indices[i];
        
        Vertex v;
        
        // Position
        if (mesh->vertex_position.exists) {
            ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
            v.position = glm::vec3(pos.x, pos.y, pos.z);
        } else {
            v.position = glm::vec3(0.0f);
        }
        
        // Normal
        if (mesh->vertex_normal.exists) {
            ufbx_vec3 norm = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
            v.normal = glm::vec3(norm.x, norm.y, norm.z);
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        
        // UV
        if (mesh->vertex_uv.exists) {
            ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
            v.uv = glm::vec2(uv.x, uv.y);
        } else {
            v.uv = glm::vec2(0.0f);
        }
        
        vertices_[i] = v;
        indices_.push_back(i); // simple 1:1 mapping since we flattened
    }

    ufbx_free_scene(scene);
    juce::Logger::writeToLog("Successfully loaded FBX into memory. Verts: " + juce::String((int)vertices_.size()));
    return true;
}

void Mesh::setupGL(juce::OpenGLContext& ctx) {
    if (vertices_.empty()) return;

    auto& ext = ctx.extensions;
    
    if (!vao_) ext.glGenVertexArrays(1, &vao_);
    if (!vbo_) ext.glGenBuffers(1, &vbo_);
    if (!ebo_) ext.glGenBuffers(1, &ebo_);

    ext.glBindVertexArray(vao_);

    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STATIC_DRAW);

    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_STATIC_DRAW);

    // Position (vec3)
    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    ext.glEnableVertexAttribArray(0);
    // Normal (vec3)
    ext.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    ext.glEnableVertexAttribArray(1);
    // UV (vec2)
    ext.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    ext.glEnableVertexAttribArray(2);

    ext.glBindVertexArray(0);
}

void Mesh::draw(juce::OpenGLContext& ctx) {
    if (!vao_ || indices_.empty()) return;
    auto& ext = ctx.extensions;
    
    ext.glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, 0);
    ext.glBindVertexArray(0);
}

// ---------------------------------------------------------
// ModelRenderer

ModelRenderer::ModelRenderer() {}
ModelRenderer::~ModelRenderer() {}

void ModelRenderer::init(juce::OpenGLContext& ctx) {
    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aUV;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 proj;
        
        out vec3 FragPos;
        out vec3 Normal;
        out vec2 UV;
        
        void main() {
            FragPos = vec3(model * vec4(aPos, 1.0));
            Normal = mat3(transpose(inverse(model))) * aNormal;
            UV = aUV;
            gl_Position = proj * view * vec4(FragPos, 1.0);
        }
    )";

    const char* fShader = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 UV;
        
        out vec4 FragColor;
        
        void main() {
            // Very simple directional light
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            vec3 norm = normalize(Normal);
            float diff = max(dot(norm, lightDir), 0.0);
            
            vec3 ambient = vec3(0.2);
            vec3 diffuse = diff * vec3(0.8);
            
            // Base color is a blank clay look for now
            vec3 objectColor = vec3(0.7, 0.7, 0.8);
            
            vec3 result = (ambient + diffuse) * objectColor;
            FragColor = vec4(result, 1.0);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();
}

void ModelRenderer::render(Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, juce::OpenGLContext& ctx) {
    if (!shader_) init(ctx);
    
    shader_->use();
    
    auto& ext = ctx.extensions;
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(shader_->getProgramID(), "model"), 1, GL_FALSE, glm::value_ptr(model));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(shader_->getProgramID(), "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(shader_->getProgramID(), "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    
    glEnable(GL_DEPTH_TEST);
    mesh.draw(ctx);
}

}

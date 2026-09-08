#include "BlockCharacter.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

// A simple 1x1x1 unit cube centered at origin
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
};

static const Vertex kCubeVerts[] = {
    // Front
    {-0.5f, -0.5f,  0.5f,  0,  0,  1}, { 0.5f, -0.5f,  0.5f,  0,  0,  1},
    { 0.5f,  0.5f,  0.5f,  0,  0,  1}, {-0.5f,  0.5f,  0.5f,  0,  0,  1},
    // Back
    {-0.5f, -0.5f, -0.5f,  0,  0, -1}, {-0.5f,  0.5f, -0.5f,  0,  0, -1},
    { 0.5f,  0.5f, -0.5f,  0,  0, -1}, { 0.5f, -0.5f, -0.5f,  0,  0, -1},
    // Left
    {-0.5f, -0.5f, -0.5f, -1,  0,  0}, {-0.5f, -0.5f,  0.5f, -1,  0,  0},
    {-0.5f,  0.5f,  0.5f, -1,  0,  0}, {-0.5f,  0.5f, -0.5f, -1,  0,  0},
    // Right
    { 0.5f, -0.5f, -0.5f,  1,  0,  0}, { 0.5f,  0.5f, -0.5f,  1,  0,  0},
    { 0.5f,  0.5f,  0.5f,  1,  0,  0}, { 0.5f, -0.5f,  0.5f,  1,  0,  0},
    // Top
    {-0.5f,  0.5f, -0.5f,  0,  1,  0}, {-0.5f,  0.5f,  0.5f,  0,  1,  0},
    { 0.5f,  0.5f,  0.5f,  0,  1,  0}, { 0.5f,  0.5f, -0.5f,  0,  1,  0},
    // Bottom
    {-0.5f, -0.5f, -0.5f,  0, -1,  0}, { 0.5f, -0.5f, -0.5f,  0, -1,  0},
    { 0.5f, -0.5f,  0.5f,  0, -1,  0}, {-0.5f, -0.5f,  0.5f,  0, -1,  0}
};

static const uint32_t kCubeIndices[] = {
    0, 1, 2,  2, 3, 0,
    4, 5, 6,  6, 7, 4,
    8, 9, 10, 10,11,8,
    12,13,14, 14,15,12,
    16,17,18, 18,19,16,
    20,21,22, 22,23,20
};

void BlockCharacter::Node::render(juce::OpenGLShaderProgram& shader, glm::mat4 parentTransform, GLuint vao, int indexCount) {
    glm::mat4 model = parentTransform;
    
    model = glm::translate(model, offset);
    model = glm::translate(model, pivot);
    
    // Apply rotations
    if (rotation.x != 0.f) model = glm::rotate(model, rotation.x, glm::vec3(1, 0, 0));
    if (rotation.y != 0.f) model = glm::rotate(model, rotation.y, glm::vec3(0, 1, 0));
    if (rotation.z != 0.f) model = glm::rotate(model, rotation.z, glm::vec3(0, 0, 1));
    
    model = glm::translate(model, -pivot);
    
    // Save model for children before scaling
    glm::mat4 childTransform = model;
    
    model = glm::scale(model, scale);

    // Set uniforms
    juce::OpenGLShaderProgram::Uniform uModel(shader, "uModel");
    uModel.setMatrix4(glm::value_ptr(model), 1, GL_FALSE);
    
    juce::OpenGLShaderProgram::Uniform uColor(shader, "uColor");
    uColor.set(color.getFloatRed(), color.getFloatGreen(), color.getFloatBlue(), 1.0f);

    // Draw cube
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Render children
    for (auto& child : children) {
        child->render(shader, childTransform, vao, indexCount);
    }
}

BlockCharacter::BlockCharacter() {
    root_ = std::make_unique<Node>();
    
    // Construct hierarchy (sizes roughly in meters)
    
    auto torso = std::make_unique<Node>();
    torso->offset = {0.0f, 0.9f, 0.0f}; // Height of pelvis
    torso->scale = {0.4f, 0.6f, 0.2f};
    torso->color = juce::Colour(0xff2244aa); // Blue shirt
    torso_ = torso.get();
    
    auto head = std::make_unique<Node>();
    head->offset = {0.0f, 0.4f, 0.0f}; // Relative to torso center (which is 0.6 tall, so half is 0.3 + 0.1 gap)
    head->pivot = {0.0f, -0.1f, 0.0f}; // Neck pivot
    head->scale = {0.3f, 0.3f, 0.3f};
    head->color = juce::Colour(0xffffccaa); // Skin
    head_ = head.get();
    
    auto leftArm = std::make_unique<Node>();
    leftArm->offset = {-0.3f, 0.2f, 0.0f}; // Left shoulder
    leftArm->pivot = {0.0f, 0.2f, 0.0f};   // Shoulder pivot
    leftArm->scale = {0.15f, 0.6f, 0.15f};
    leftArm->color = juce::Colour(0xffffccaa);
    leftArm_ = leftArm.get();
    
    auto rightArm = std::make_unique<Node>();
    rightArm->offset = {0.3f, 0.2f, 0.0f}; // Right shoulder
    rightArm->pivot = {0.0f, 0.2f, 0.0f};
    rightArm->scale = {0.15f, 0.6f, 0.15f};
    rightArm->color = juce::Colour(0xffffccaa);
    rightArm_ = rightArm.get();

    auto leftLeg = std::make_unique<Node>();
    leftLeg->offset = {-0.12f, -0.4f, 0.0f}; // Left hip
    leftLeg->pivot = {0.0f, 0.3f, 0.0f};     // Hip pivot
    leftLeg->scale = {0.16f, 0.7f, 0.16f};
    leftLeg->color = juce::Colour(0xff333333); // Dark pants
    leftLeg_ = leftLeg.get();
    
    auto rightLeg = std::make_unique<Node>();
    rightLeg->offset = {0.12f, -0.4f, 0.0f};
    rightLeg->pivot = {0.0f, 0.3f, 0.0f};
    rightLeg->scale = {0.16f, 0.7f, 0.16f};
    rightLeg->color = juce::Colour(0xff333333);
    rightLeg_ = rightLeg.get();

    // Attach to Torso
    torso->children.push_back(std::move(head));
    torso->children.push_back(std::move(leftArm));
    torso->children.push_back(std::move(rightArm));
    torso->children.push_back(std::move(leftLeg));
    torso->children.push_back(std::move(rightLeg));

    root_->children.push_back(std::move(torso));
}

BlockCharacter::~BlockCharacter() {
    if (vao_) ctx_->extensions.glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
}

void BlockCharacter::initialise(juce::OpenGLContext& ctx) {
    ctx_ = &ctx;

    ctx.extensions.glGenVertexArrays(1, &vao_);
    ctx.extensions.glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIndices), kCubeIndices, GL_STATIC_DRAW);

    ctx.extensions.glBindVertexArray(0);
}

void BlockCharacter::update(float dt, const glm::vec3& velocity) {
    float speed = glm::length(glm::vec2(velocity.x, velocity.z));
    
    if (speed > 0.1f) {
        walkPhase_ += dt * speed * 2.0f; // Scale phase by speed
    } else {
        // Return to idle slowly
        walkPhase_ = std::fmod(walkPhase_, glm::two_pi<float>());
        if (walkPhase_ > glm::pi<float>()) walkPhase_ -= glm::two_pi<float>();
        walkPhase_ *= std::exp(-5.0f * dt);
    }
    
    // Sine wave for arms/legs
    float swing = std::sin(walkPhase_) * 0.8f;
    
    if (leftArm_) leftArm_->rotation.x = swing;
    if (rightArm_) rightArm_->rotation.x = -swing;
    if (leftLeg_) leftLeg_->rotation.x = -swing;
    if (rightLeg_) rightLeg_->rotation.x = swing;
    
    // Tiny head bob
    if (head_) head_->rotation.y = std::sin(walkPhase_ * 0.5f) * 0.1f;
}

void BlockCharacter::render(juce::OpenGLShaderProgram& shader, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& modelTransform, const glm::vec3& camPos) {
    shader.use();
    
    juce::OpenGLShaderProgram::Uniform uVP(shader, "uVP");
    uVP.setMatrix4(glm::value_ptr(proj * view), 1, GL_FALSE);
    
    juce::OpenGLShaderProgram::Uniform uCamPos(shader, "uCamPos");
    uCamPos.set(camPos.x, camPos.y, camPos.z);
    
    if (root_) {
        root_->render(shader, modelTransform, vao_, 36);
    }
}

} // namespace Harmonia

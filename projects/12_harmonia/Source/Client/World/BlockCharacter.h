#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Harmonia {

class BlockCharacter {
public:
    BlockCharacter();
    ~BlockCharacter();

    void initialise(juce::OpenGLContext& ctx);
    void update(float dt, const glm::vec3& velocity);
    void render(juce::OpenGLShaderProgram& shader, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& modelTransform, const glm::vec3& camPos);

private:
    struct Node {
        glm::vec3 offset{0.f};
        glm::vec3 pivot{0.f};
        glm::vec3 scale{1.f};
        glm::vec3 rotation{0.f}; // Euler angles
        juce::Colour color{juce::Colours::white};
        std::vector<std::unique_ptr<Node>> children;
        
        void render(juce::OpenGLShaderProgram& shader, glm::mat4 parentTransform, GLuint vao, int indexCount);
    };

    std::unique_ptr<Node> root_;
    Node* torso_ = nullptr;
    Node* head_ = nullptr;
    Node* leftArm_ = nullptr;
    Node* rightArm_ = nullptr;
    Node* leftLeg_ = nullptr;
    Node* rightLeg_ = nullptr;

    float walkPhase_ = 0.f;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    juce::OpenGLContext* ctx_ = nullptr;
};

} // namespace Harmonia

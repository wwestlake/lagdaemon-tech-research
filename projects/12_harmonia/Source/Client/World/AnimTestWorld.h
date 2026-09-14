#pragma once
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <juce_opengl/juce_opengl.h>
#include "Client/Engine/Rendering/Camera.h"
#include "Client/Engine/Rendering/GltfCharacter.h"
#include "Client/Engine/Animation/LocomotionController.h"
#include "Client/Physics/PhysicsWorld.h"
#include "Client/World/PlayerController.h"
#include "Shared/Core/TripleBuffer.h"

namespace Harmonia {

struct AnimTestRenderState {
    glm::mat4 cameraView{1.0f};
    glm::mat4 cameraProj{1.0f};
    glm::vec3 cameraPos{0.0f};
    
    glm::mat4 charTransform{1.0f};
    std::vector<glm::mat4> charBoneTransforms; // Not strictly needed anymore if we just pass vertex data!
    std::vector<float> charVertexData;
};

class AnimTestWorld {
public:
    AnimTestWorld(Camera& camera);
    ~AnimTestWorld();

    void update(float dt);
    void render(float aspectRatio, juce::OpenGLContext& ctx, const glm::vec3& sunDir, const glm::vec3& sunColor);
    
    void onClick();

private:
    Camera& camera_;
    TripleBuffer<AnimTestRenderState> renderStateBuffer_;
    
    std::unique_ptr<GltfCharacter> playerChar_;
    std::unique_ptr<djehuti::animation::LocomotionController> procAnim_;
    
    std::unique_ptr<PhysicsWorld> physics_;
    PlayerController localPlayer_;
    
    std::unique_ptr<juce::OpenGLShaderProgram> labShader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int numTriVerts_ = 0;
    int numLineVerts_ = 0;
    
    // Animation state for dummy character
    bool walking_ = false;
    bool wWasDown_ = false;
    float characterYaw_ = 3.14159265f;
    std::string currentClip_ = "Idle";
};

} // namespace Harmonia

#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Harmonia {

// Loads a single skinned/animated glTF character (GLTF_SEPARATE - one
// .gltf JSON + one .bin buffer, no textures) and plays back one of its
// baked animation clips via CPU skinning. Deliberately scoped to exactly
// what this project's exported assets need - one mesh, one skin, no
// materials/textures (rendered as a flat stylised colour instead) - not
// a general-purpose glTF importer.
class GltfCharacter {
public:
    GltfCharacter();
    ~GltfCharacter();

    // path is the .gltf file; the .bin buffer is expected alongside it,
    // named exactly as the glTF's own "buffers[0].uri" says.
    bool load(const juce::File& gltfPath);

    void playClip(const std::string& name); // no-op if the clip isn't loaded
    void update(float dt);
    void render(juce::OpenGLContext& ctx, const glm::mat4& view, const glm::mat4& proj,
                const glm::mat4& modelTransform, const glm::vec3& sunDir, const glm::vec3& sunColor,
                const glm::vec3& color);

private:
    struct Node {
        std::string name;
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        std::vector<int> children;
    };

    struct Keyframe3  { float time; glm::vec3 value; };
    struct KeyframeQ  { float time; glm::quat value; };

    struct NodeAnim {
        std::vector<Keyframe3> translation;
        std::vector<KeyframeQ> rotation;
        std::vector<Keyframe3> scale;
    };

    struct Clip {
        std::unordered_map<int, NodeAnim> perNode; // node index -> animation
        float duration = 0.0f;
    };

    void initGL(juce::OpenGLContext& ctx);
    glm::mat4 localTransform(int nodeIndex, const Clip* clip, float t) const;
    void computeGlobalTransforms(std::vector<glm::mat4>& outGlobal, const Clip* clip, float t) const;
    void skinVertices();

    // Parsed glTF data
    std::vector<Node> nodes_;
    std::vector<int> rootNodes_;
    std::vector<int> jointNodes_;              // skins[0].joints
    std::vector<glm::mat4> inverseBindMatrices_;
    std::unordered_map<std::string, Clip> clips_;

    // Mesh data (bind pose, read once)
    std::vector<glm::vec3> bindPositions_;
    std::vector<glm::vec3> bindNormals_;
    std::vector<glm::ivec4> jointIndices_;
    std::vector<glm::vec4> jointWeights_;
    std::vector<uint32_t> indices_;

    // Per-frame CPU-skinned output, re-uploaded to the GPU each frame
    std::vector<float> skinnedVertexData_; // pos.xyz, normal.xyz interleaved

    std::string currentClip_;
    float playTime_ = 0.0f;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> shader_;
    bool glInitialised_ = false;
    bool loaded_ = false;
};

}

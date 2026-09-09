#include "GltfCharacter.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

using namespace juce::gl;

namespace Harmonia {

namespace {

// --- Minimal glTF accessor/bufferView reading -----------------------------
// Scoped to exactly what this project's own Blender-exported assets
// produce (GLTF_SEPARATE, one external .bin, tightly packed accessors) -
// not a general-purpose glTF reader.

struct RawBuffer {
    std::vector<uint8_t> bytes;
};

size_t componentSize(int componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1; // BYTE / UNSIGNED_BYTE
        case 5122: case 5123: return 2; // SHORT / UNSIGNED_SHORT
        case 5125: case 5126: return 4; // UNSIGNED_INT / FLOAT
        default: return 4;
    }
}

int typeComponentCount(const juce::String& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 1;
}

// Reads accessor `accessorIndex` as a flat float array (upcasting ints as
// needed) - covers POSITION/NORMAL/WEIGHTS_0 (float) and, with
// normalize=false, integer accessors like JOINTS_0 read as raw indices.
std::vector<float> readAccessorFloats(const juce::var& root, const RawBuffer& buf, int accessorIndex) {
    auto accessors = root["accessors"];
    auto accessor = accessors[accessorIndex];
    int bufferViewIndex = (int)accessor["bufferView"];
    auto bufferViews = root["bufferViews"];
    auto bufferView = bufferViews[bufferViewIndex];

    int bvByteOffset = bufferView.hasProperty("byteOffset") ? (int)bufferView["byteOffset"] : 0;
    int accByteOffset = accessor.hasProperty("byteOffset") ? (int)accessor["byteOffset"] : 0;
    int componentType = (int)accessor["componentType"];
    juce::String type = accessor["type"].toString();
    int count = (int)accessor["count"];
    int numComponents = typeComponentCount(type);
    size_t compSize = componentSize(componentType);
    int stride = bufferView.hasProperty("byteStride") ? (int)bufferView["byteStride"] : (int)(compSize * numComponents);

    const uint8_t* base = buf.bytes.data() + bvByteOffset + accByteOffset;

    std::vector<float> out;
    out.reserve((size_t)count * numComponents);
    for (int i = 0; i < count; ++i) {
        const uint8_t* elem = base + (size_t)i * stride;
        for (int c = 0; c < numComponents; ++c) {
            const uint8_t* p = elem + c * compSize;
            float v = 0.0f;
            switch (componentType) {
                case 5126: { float f; std::memcpy(&f, p, 4); v = f; break; }               // FLOAT
                case 5125: { uint32_t u; std::memcpy(&u, p, 4); v = (float)u; break; }      // UNSIGNED_INT
                case 5123: { uint16_t u; std::memcpy(&u, p, 2); v = (float)u; break; }      // UNSIGNED_SHORT
                case 5121: { uint8_t u = *p; v = (float)u; break; }                          // UNSIGNED_BYTE
                case 5122: { int16_t s; std::memcpy(&s, p, 2); v = (float)s; break; }        // SHORT
                case 5120: { int8_t s = (int8_t)*p; v = (float)s; break; }                   // BYTE
                default: break;
            }
            out.push_back(v);
        }
    }
    return out;
}

glm::quat quatFrom(const std::vector<float>& v, size_t i) {
    // glTF stores quaternions as (x, y, z, w)
    return glm::quat(v[i + 3], v[i + 0], v[i + 1], v[i + 2]);
}

} // namespace

GltfCharacter::GltfCharacter() {}
GltfCharacter::~GltfCharacter() {}

bool GltfCharacter::load(const juce::File& gltfPath) {
    juce::var root = juce::JSON::parse(gltfPath);
    if (!root.isObject()) return false;

    // --- Buffer ---
    juce::String bufferUri = root["buffers"][0]["uri"].toString();
    juce::File binFile = gltfPath.getSiblingFile(bufferUri);
    if (!binFile.existsAsFile()) return false;
    RawBuffer buf;
    juce::MemoryBlock mb;
    binFile.loadFileAsData(mb);
    buf.bytes.assign((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize());

    // --- Nodes ---
    auto nodesVar = root["nodes"];
    int nodeCount = nodesVar.size();
    nodes_.resize((size_t)nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        auto n = nodesVar[i];
        Node& node = nodes_[(size_t)i];
        node.name = n["name"].toString().toStdString();
        if (n.hasProperty("translation")) {
            auto t = n["translation"];
            node.translation = glm::vec3((float)t[0], (float)t[1], (float)t[2]);
        }
        if (n.hasProperty("rotation")) {
            auto r = n["rotation"];
            node.rotation = glm::quat((float)r[3], (float)r[0], (float)r[1], (float)r[2]);
        }
        if (n.hasProperty("scale")) {
            auto s = n["scale"];
            node.scale = glm::vec3((float)s[0], (float)s[1], (float)s[2]);
        }
        if (n.hasProperty("children")) {
            auto ch = n["children"];
            for (int c = 0; c < ch.size(); ++c) node.children.push_back((int)ch[c]);
        }
    }
    auto sceneNodes = root["scenes"][(int)root["scene"]]["nodes"];
    for (int i = 0; i < sceneNodes.size(); ++i) rootNodes_.push_back((int)sceneNodes[i]);

    // --- Skin ---
    auto skin = root["skins"][0];
    auto joints = skin["joints"];
    for (int i = 0; i < joints.size(); ++i) jointNodes_.push_back((int)joints[i]);
    if (skin.hasProperty("inverseBindMatrices")) {
        auto ibm = readAccessorFloats(root, buf, (int)skin["inverseBindMatrices"]);
        inverseBindMatrices_.resize(jointNodes_.size());
        for (size_t i = 0; i < jointNodes_.size(); ++i) {
            inverseBindMatrices_[i] = glm::make_mat4(&ibm[i * 16]);
        }
    }

    // --- Mesh (single primitive) ---
    auto primitive = root["meshes"][0]["primitives"][0];
    auto attrs = primitive["attributes"];

    auto posRaw = readAccessorFloats(root, buf, (int)attrs["POSITION"]);
    size_t vertCount = posRaw.size() / 3;
    bindPositions_.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i)
        bindPositions_[i] = glm::vec3(posRaw[i*3], posRaw[i*3+1], posRaw[i*3+2]);

    if (attrs.hasProperty("NORMAL")) {
        auto nrmRaw = readAccessorFloats(root, buf, (int)attrs["NORMAL"]);
        bindNormals_.resize(vertCount);
        for (size_t i = 0; i < vertCount; ++i)
            bindNormals_[i] = glm::vec3(nrmRaw[i*3], nrmRaw[i*3+1], nrmRaw[i*3+2]);
    } else {
        bindNormals_.assign(vertCount, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    auto jointsRaw = readAccessorFloats(root, buf, (int)attrs["JOINTS_0"]);
    jointIndices_.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i)
        jointIndices_[i] = glm::ivec4((int)jointsRaw[i*4], (int)jointsRaw[i*4+1], (int)jointsRaw[i*4+2], (int)jointsRaw[i*4+3]);

    auto weightsRaw = readAccessorFloats(root, buf, (int)attrs["WEIGHTS_0"]);
    jointWeights_.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i)
        jointWeights_[i] = glm::vec4(weightsRaw[i*4], weightsRaw[i*4+1], weightsRaw[i*4+2], weightsRaw[i*4+3]);

    if (primitive.hasProperty("indices")) {
        auto idxRaw = readAccessorFloats(root, buf, (int)primitive["indices"]);
        indices_.resize(idxRaw.size());
        for (size_t i = 0; i < idxRaw.size(); ++i) indices_[i] = (uint32_t)idxRaw[i];
    }

    // --- Animations ---
    auto anims = root["animations"];
    for (int a = 0; a < anims.size(); ++a) {
        auto anim = anims[a];
        Clip clip;
        auto samplers = anim["samplers"];
        auto channels = anim["channels"];
        for (int c = 0; c < channels.size(); ++c) {
            auto ch = channels[c];
            int samplerIdx = (int)ch["sampler"];
            auto sampler = samplers[samplerIdx];
            int targetNode = (int)ch["target"]["node"];
            juce::String path = ch["target"]["path"].toString();

            auto times = readAccessorFloats(root, buf, (int)sampler["input"]);
            auto values = readAccessorFloats(root, buf, (int)sampler["output"]);

            NodeAnim& na = clip.perNode[targetNode];
            if (path == "translation" || path == "scale") {
                std::vector<Keyframe3>& list = (path == "translation") ? na.translation : na.scale;
                for (size_t i = 0; i < times.size(); ++i) {
                    list.push_back({ times[i], glm::vec3(values[i*3], values[i*3+1], values[i*3+2]) });
                }
            } else if (path == "rotation") {
                for (size_t i = 0; i < times.size(); ++i) {
                    na.rotation.push_back({ times[i], quatFrom(values, i*4) });
                }
            }
            if (!times.empty() && times.back() > clip.duration) clip.duration = times.back();
        }
        clips_[anim["name"].toString().toStdString()] = std::move(clip);
    }

    skinnedVertexData_.resize(vertCount * 6);
    loaded_ = true;
    return true;
}

void GltfCharacter::playClip(const std::string& name) {
    if (!clips_.count(name)) return;
    currentClip_ = name;
    playTime_ = 0.0f;
}

void GltfCharacter::update(float dt) {
    if (!loaded_ || currentClip_.empty()) return;
    const Clip& clip = clips_[currentClip_];
    if (clip.duration <= 0.0f) return;
    playTime_ += dt;
    while (playTime_ > clip.duration) playTime_ -= clip.duration;

    skinVertices();
}

glm::mat4 GltfCharacter::localTransform(int nodeIndex, const Clip* clip, float t) const {
    const Node& node = nodes_[(size_t)nodeIndex];
    glm::vec3 translation = node.translation;
    glm::quat rotation = node.rotation;
    glm::vec3 scale = node.scale;

    if (clip) {
        auto it = clip->perNode.find(nodeIndex);
        if (it != clip->perNode.end()) {
            const NodeAnim& na = it->second;
            auto sampleVec3 = [&](const std::vector<Keyframe3>& keys, const glm::vec3& fallback) {
                if (keys.empty()) return fallback;
                if (keys.size() == 1 || t <= keys.front().time) return keys.front().value;
                if (t >= keys.back().time) return keys.back().value;
                for (size_t i = 0; i + 1 < keys.size(); ++i) {
                    if (t >= keys[i].time && t <= keys[i+1].time) {
                        float span = keys[i+1].time - keys[i].time;
                        float a = span > 0.0f ? (t - keys[i].time) / span : 0.0f;
                        return glm::mix(keys[i].value, keys[i+1].value, a);
                    }
                }
                return keys.back().value;
            };
            if (!na.translation.empty()) translation = sampleVec3(na.translation, translation);
            if (!na.scale.empty()) scale = sampleVec3(na.scale, scale);
            if (!na.rotation.empty()) {
                const auto& keys = na.rotation;
                if (keys.size() == 1 || t <= keys.front().time) rotation = keys.front().value;
                else if (t >= keys.back().time) rotation = keys.back().value;
                else {
                    for (size_t i = 0; i + 1 < keys.size(); ++i) {
                        if (t >= keys[i].time && t <= keys[i+1].time) {
                            float span = keys[i+1].time - keys[i].time;
                            float a = span > 0.0f ? (t - keys[i].time) / span : 0.0f;
                            rotation = glm::slerp(keys[i].value, keys[i+1].value, a);
                            break;
                        }
                    }
                }
            }
        }
    }

    glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
    m *= glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

void GltfCharacter::computeGlobalTransforms(std::vector<glm::mat4>& outGlobal, const Clip* clip, float t) const {
    outGlobal.assign(nodes_.size(), glm::mat4(1.0f));
    std::vector<bool> visited(nodes_.size(), false);

    // Iterative stack walk from each root, parent-before-children.
    std::vector<std::pair<int, glm::mat4>> stack;
    for (int r : rootNodes_) stack.push_back({ r, glm::mat4(1.0f) });

    while (!stack.empty()) {
        auto [idx, parentGlobal] = stack.back();
        stack.pop_back();
        glm::mat4 global = parentGlobal * localTransform(idx, clip, t);
        outGlobal[(size_t)idx] = global;
        visited[(size_t)idx] = true;
        for (int c : nodes_[(size_t)idx].children) stack.push_back({ c, global });
    }
}

void GltfCharacter::skinVertices() {
    std::vector<glm::mat4> globals;
    const Clip* clip = currentClip_.empty() ? nullptr : &clips_[currentClip_];
    computeGlobalTransforms(globals, clip, playTime_);

    std::vector<glm::mat4> jointMatrices(jointNodes_.size());
    for (size_t j = 0; j < jointNodes_.size(); ++j) {
        jointMatrices[j] = globals[(size_t)jointNodes_[j]] * inverseBindMatrices_[j];
    }

    size_t vertCount = bindPositions_.size();
    for (size_t i = 0; i < vertCount; ++i) {
        glm::vec4 w = jointWeights_[i];
        glm::ivec4 j = jointIndices_[i];
        glm::mat4 skin =
            w.x * jointMatrices[(size_t)j.x] +
            w.y * jointMatrices[(size_t)j.y] +
            w.z * jointMatrices[(size_t)j.z] +
            w.w * jointMatrices[(size_t)j.w];

        glm::vec3 pos = glm::vec3(skin * glm::vec4(bindPositions_[i], 1.0f));
        glm::vec3 nrm = glm::normalize(glm::mat3(skin) * bindNormals_[i]);

        skinnedVertexData_[i*6 + 0] = pos.x;
        skinnedVertexData_[i*6 + 1] = pos.y;
        skinnedVertexData_[i*6 + 2] = pos.z;
        skinnedVertexData_[i*6 + 3] = nrm.x;
        skinnedVertexData_[i*6 + 4] = nrm.y;
        skinnedVertexData_[i*6 + 5] = nrm.z;
    }
}

void GltfCharacter::initGL(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;

    ext.glGenVertexArrays(1, &vao_);
    ext.glBindVertexArray(vao_);

    ext.glGenBuffers(1, &vbo_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(skinnedVertexData_.size() * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);

    ext.glGenBuffers(1, &ebo_);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices_.size() * sizeof(uint32_t)), indices_.data(), GL_STATIC_DRAW);

    const int stride = 6 * sizeof(float);
    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    ext.glEnableVertexAttribArray(1);

    ext.glBindVertexArray(0);

    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 proj;
        out vec3 worldNormal;
        out vec3 worldPos;
        void main() {
            vec4 wp = model * vec4(aPos, 1.0);
            worldPos = wp.xyz;
            worldNormal = mat3(model) * aNormal;
            gl_Position = proj * view * wp;
        }
    )";

    // Stylised "polished car paint" look: diffuse from the real sun plus a
    // tight Blinn-Phong specular highlight and a soft fresnel-ish rim, so
    // a flat solid colour still reads as shiny, not matte.
    const char* fShader = R"(
        #version 330 core
        in vec3 worldNormal;
        in vec3 worldPos;
        out vec4 FragColor;
        uniform vec3 baseColor;
        uniform vec3 sunDir;
        uniform vec3 sunColor;
        uniform vec3 camPos;
        void main() {
            vec3 N = normalize(worldNormal);
            vec3 L = normalize(sunDir);
            vec3 V = normalize(camPos - worldPos);
            vec3 H = normalize(L + V);

            float diffuse = 0.35 + 0.65 * max(dot(N, L), 0.0);
            float spec = pow(max(dot(N, H), 0.0), 48.0);
            float fresnel = pow(1.0 - max(dot(N, V), 0.0), 2.5);

            vec3 col = baseColor * diffuse;
            col = mix(col, col * sunColor, 0.2);
            col += spec * sunColor * 1.5;
            col += fresnel * 0.25;

            FragColor = vec4(col, 1.0);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();

    glInitialised_ = true;
}

void GltfCharacter::render(juce::OpenGLContext& ctx, const glm::mat4& view, const glm::mat4& proj,
                            const glm::mat4& modelTransform, const glm::vec3& sunDir, const glm::vec3& sunColor,
                            const glm::vec3& color) {
    if (!loaded_) return;
    if (!glInitialised_) initGL(ctx);
    if (!shader_) return;

    auto& ext = ctx.extensions;
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(skinnedVertexData_.size() * sizeof(float)), skinnedVertexData_.data());

    shader_->use();
    GLint progId = (GLint)shader_->getProgramID();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "model"), 1, GL_FALSE, glm::value_ptr(modelTransform));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    ext.glUniform3f(ext.glGetUniformLocation(progId, "baseColor"), color.x, color.y, color.z);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunDir"), sunDir.x, sunDir.y, sunDir.z);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunColor"), sunColor.x, sunColor.y, sunColor.z);
    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "camPos"), camPos.x, camPos.y, camPos.z);

    ext.glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, (void*)0);
    ext.glBindVertexArray(0);
}

}

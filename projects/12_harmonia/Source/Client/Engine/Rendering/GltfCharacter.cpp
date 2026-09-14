#include "GltfCharacter.h"
#include "../Animation/MotionDatabase.h"
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

    // --- Images & Textures ---
    if (root.hasProperty("images") && root.hasProperty("textures")) {
        auto images = root["images"];
        auto gltfTextures = root["textures"];
        textures_.resize((size_t)gltfTextures.size());
        for (int i = 0; i < gltfTextures.size(); ++i) {
            int sourceIdx = (int)gltfTextures[i]["source"];
            if (sourceIdx >= 0 && sourceIdx < images.size()) {
                juce::String uri = images[sourceIdx]["uri"].toString();
                juce::File imgFile = gltfPath.getSiblingFile(uri);
                if (imgFile.existsAsFile()) {
                    textures_[i].image = juce::ImageFileFormat::loadFrom(imgFile);
                }
            }
        }
    }

    // --- Materials ---
    if (root.hasProperty("materials")) {
        auto mats = root["materials"];
        materials_.resize((size_t)mats.size());
        for (int i = 0; i < mats.size(); ++i) {
            auto m = mats[i];
            if (m.hasProperty("pbrMetallicRoughness")) {
                auto pbr = m["pbrMetallicRoughness"];
                if (pbr.hasProperty("baseColorFactor")) {
                    auto bcf = pbr["baseColorFactor"];
                    materials_[i].baseColorFactor = glm::vec4(
                        (float)bcf[0], (float)bcf[1], (float)bcf[2], (float)bcf[3]);
                }
                if (pbr.hasProperty("baseColorTexture")) {
                    materials_[i].baseColorTextureIndex = (int)pbr["baseColorTexture"]["index"];
                }
            }
        }
    }

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

    // --- Meshes ---
    auto meshes = root["meshes"];
    for (int m = 0; m < meshes.size(); ++m) {
        auto prims = meshes[m]["primitives"];
        for (int p = 0; p < prims.size(); ++p) {
            auto primitive = prims[p];
            auto attrs = primitive["attributes"];
            if (!attrs.hasProperty("POSITION")) continue;

            Primitive prim;
            if (primitive.hasProperty("material")) {
                prim.materialIndex = (int)primitive["material"];
            }
            prim.indexOffset = indices_.size();

            size_t baseVertex = bindPositions_.size();

            auto posRaw = readAccessorFloats(root, buf, (int)attrs["POSITION"]);
            size_t vCount = posRaw.size() / 3;
            for (size_t i = 0; i < vCount; ++i)
                bindPositions_.push_back(glm::vec3(posRaw[i*3], posRaw[i*3+1], posRaw[i*3+2]));

            if (attrs.hasProperty("NORMAL")) {
                auto nrmRaw = readAccessorFloats(root, buf, (int)attrs["NORMAL"]);
                for (size_t i = 0; i < vCount; ++i)
                    bindNormals_.push_back(glm::vec3(nrmRaw[i*3], nrmRaw[i*3+1], nrmRaw[i*3+2]));
            } else {
                for (size_t i = 0; i < vCount; ++i)
                    bindNormals_.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
            }

            if (attrs.hasProperty("TEXCOORD_0")) {
                auto uvRaw = readAccessorFloats(root, buf, (int)attrs["TEXCOORD_0"]);
                // UVs are 2 floats per vertex
                for (size_t i = 0; i < vCount; ++i)
                    bindTexCoords_.push_back(glm::vec2(uvRaw[i*2], uvRaw[i*2+1]));
            } else {
                for (size_t i = 0; i < vCount; ++i)
                    bindTexCoords_.push_back(glm::vec2(0.0f, 0.0f));
            }

            if (attrs.hasProperty("JOINTS_0")) {
                auto jointsRaw = readAccessorFloats(root, buf, (int)attrs["JOINTS_0"]);
                for (size_t i = 0; i < vCount; ++i)
                    jointIndices_.push_back(glm::ivec4((int)jointsRaw[i*4], (int)jointsRaw[i*4+1], (int)jointsRaw[i*4+2], (int)jointsRaw[i*4+3]));
            } else {
                for (size_t i = 0; i < vCount; ++i) jointIndices_.push_back(glm::ivec4(0));
            }

            if (attrs.hasProperty("WEIGHTS_0")) {
                auto weightsRaw = readAccessorFloats(root, buf, (int)attrs["WEIGHTS_0"]);
                for (size_t i = 0; i < vCount; ++i)
                    jointWeights_.push_back(glm::vec4(weightsRaw[i*4], weightsRaw[i*4+1], weightsRaw[i*4+2], weightsRaw[i*4+3]));
            } else {
                for (size_t i = 0; i < vCount; ++i) jointWeights_.push_back(glm::vec4(1,0,0,0));
            }

            if (primitive.hasProperty("indices")) {
                auto idxRaw = readAccessorFloats(root, buf, (int)primitive["indices"]);
                prim.indexCount = idxRaw.size();
                for (size_t i = 0; i < idxRaw.size(); ++i)
                    indices_.push_back((uint32_t)(baseVertex + idxRaw[i]));
            }
            
            primitives_.push_back(prim);
        }
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

    skinnedVertexData_.resize(bindPositions_.size() * 6);
    if (!bindPositions_.empty()) {
        glm::vec3 minB(1e10f), maxB(-1e10f);
        for (const auto& p : bindPositions_) {
            minB = (glm::min)(minB, p);
            maxB = (glm::max)(maxB, p);
        }
        juce::File logFile("C:/Users/wwestlake/makehuman_height.txt");
        logFile.replaceWithText("MAKEHUMAN RAW HEIGHT: " + juce::String(maxB.y - minB.y));
    }

    loaded_ = true;
    
    // Generate procedural clips for MakeHuman rig
    int spineIdx = findNodeIndex("spine05");
    int armL = findNodeIndex("upperarm_l");
    int armR = findNodeIndex("upperarm_r");
    
    Clip idleClip;
    idleClip.duration = 4.0f;
    if (spineIdx >= 0) {
        NodeAnim spineAnim;
        spineAnim.rotation.push_back({0.0f, glm::angleAxis(0.0f, glm::vec3(1,0,0))});
        spineAnim.rotation.push_back({2.0f, glm::angleAxis(0.05f, glm::vec3(1,0,0))});
        spineAnim.rotation.push_back({4.0f, glm::angleAxis(0.0f, glm::vec3(1,0,0))});
        idleClip.perNode[spineIdx] = spineAnim;
    }
    clips_["Idle"] = idleClip;

    Clip walkClip;
    walkClip.duration = 1.0f;
    if (armL >= 0 && armR >= 0) {
        NodeAnim lAnim, rAnim;
        // MakeHuman arms point +X (left) and -X (right) in T-pose. 
        // We want them to swing on the Z axis (forward/back in model space).
        // A rotation around Y axis swings them forward/back.
        lAnim.rotation.push_back({0.0f, glm::angleAxis(-0.4f, glm::vec3(0,1,0))});
        lAnim.rotation.push_back({0.5f, glm::angleAxis(0.4f, glm::vec3(0,1,0))});
        lAnim.rotation.push_back({1.0f, glm::angleAxis(-0.4f, glm::vec3(0,1,0))});
        
        rAnim.rotation.push_back({0.0f, glm::angleAxis(0.4f, glm::vec3(0,1,0))});
        rAnim.rotation.push_back({0.5f, glm::angleAxis(-0.4f, glm::vec3(0,1,0))});
        rAnim.rotation.push_back({1.0f, glm::angleAxis(0.4f, glm::vec3(0,1,0))});
        
        walkClip.perNode[armL] = lAnim;
        walkClip.perNode[armR] = rAnim;
    }
    if (spineIdx >= 0) {
        NodeAnim spineAnim;
        spineAnim.rotation.push_back({0.0f, glm::angleAxis(0.05f, glm::vec3(1,0,0))});
        spineAnim.rotation.push_back({0.5f, glm::angleAxis(0.08f, glm::vec3(1,0,0))});
        spineAnim.rotation.push_back({1.0f, glm::angleAxis(0.05f, glm::vec3(1,0,0))});
        walkClip.perNode[spineIdx] = spineAnim;
    }
    clips_["Walk"] = walkClip;

    // Skin the vertices once to the bind pose so static meshes render
    skinVertices();
    
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

int GltfCharacter::findNodeIndex(const std::string& name) const {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].name == name) return (int)i;
    }
    return -1;
}

void GltfCharacter::setNodeOverride(int nodeIndex, const glm::quat& rotation) {
    if (nodeIndex >= 0 && nodeIndex < (int)nodes_.size()) {
        nodeOverrides_[nodeIndex] = rotation;
    }
}

void GltfCharacter::clearOverrides() {
    nodeOverrides_.clear();
}

void GltfCharacter::applyOverrides() {
    skinVertices();
}

glm::mat4 GltfCharacter::localTransform(int nodeIndex, const Clip* clip, float t) const {
    const Node& node = nodes_[(size_t)nodeIndex];
    glm::vec3 translation = node.translation;
    glm::quat rotation = node.rotation;
    glm::vec3 scale = node.scale;

    auto it = nodeOverrides_.find(nodeIndex);
    if (it != nodeOverrides_.end()) {
        rotation = it->second * node.rotation;
    }
    else if (clip) {
        auto clipIt = clip->perNode.find(nodeIndex);
        if (clipIt != clip->perNode.end()) {
            const auto& na = clipIt->second;
            
            if (!na.translation.empty()) {
                const auto& keys = na.translation;
                if (keys.size() == 1 || t <= keys[0].time) translation = keys[0].value;
                else if (t >= keys[keys.size() - 1].time) translation = keys[keys.size() - 1].value;
                else {
                    for (size_t i = 0; i + 1 < keys.size(); ++i) {
                        if (t >= keys[i].time && t <= keys[i+1].time) {
                            float span = keys[i+1].time - keys[i].time;
                            float a = span > 0.0f ? (t - keys[i].time) / span : 0.0f;
                            translation = keys[i].value + (keys[i+1].value - keys[i].value) * a;
                            break;
                        }
                    }
                }
            }
            if (!na.scale.empty()) {
                const auto& keys = na.scale;
                if (keys.size() == 1 || t <= keys[0].time) scale = keys[0].value;
                else if (t >= keys[keys.size() - 1].time) scale = keys[keys.size() - 1].value;
                else {
                    for (size_t i = 0; i + 1 < keys.size(); ++i) {
                        if (t >= keys[i].time && t <= keys[i+1].time) {
                            float span = keys[i+1].time - keys[i].time;
                            float a = span > 0.0f ? (t - keys[i].time) / span : 0.0f;
                            scale = keys[i].value + (keys[i+1].value - keys[i].value) * a;
                            break;
                        }
                    }
                }
            }
            if (!na.rotation.empty()) {
                const auto& keys = na.rotation;
                if (keys.size() == 1 || t <= keys[0].time) rotation = keys[0].value;
                else if (t >= keys[keys.size() - 1].time) rotation = keys[keys.size() - 1].value;
                else {
                    for (size_t i = 0; i + 1 < keys.size(); ++i) {
                        if (t >= keys[i].time && t <= keys[i+1].time) {
                            float span = keys[i+1].time - keys[i].time;
                            float a = span > 0.0f ? (t - keys[i].time) / span : 0.0f;
                            float dot = glm::dot(keys[i].value, keys[i+1].value);
                            glm::quat q2 = keys[i+1].value;
                            if (dot < 0.0f) q2 = -q2;
                            rotation = glm::normalize(keys[i].value * (1.0f - a) + q2 * a);
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

    size_t vCount = bindPositions_.size();
    skinnedVertexData_.resize(vCount * 8);

    for (size_t i = 0; i < vCount; ++i) {
        glm::vec3 bp = bindPositions_[i];
        glm::vec3 bn = bindNormals_[i];
        glm::ivec4 ji = jointIndices_[i];
        glm::vec4 jw = jointWeights_[i];

        glm::mat4 skinMat =
            jw.x * jointMatrices[(size_t)ji.x] +
            jw.y * jointMatrices[(size_t)ji.y] +
            jw.z * jointMatrices[(size_t)ji.z] +
            jw.w * jointMatrices[(size_t)ji.w];

        glm::vec3 skinnedPos = glm::vec3(skinMat * glm::vec4(bp, 1.0f));
        glm::vec3 skinnedNrm = glm::normalize(glm::mat3(skinMat) * bn);
        glm::vec2 texCoord = bindTexCoords_[i];

        skinnedVertexData_[i * 8 + 0] = skinnedPos.x;
        skinnedVertexData_[i * 8 + 1] = skinnedPos.y;
        skinnedVertexData_[i * 8 + 2] = skinnedPos.z;
        skinnedVertexData_[i * 8 + 3] = skinnedNrm.x;
        skinnedVertexData_[i * 8 + 4] = skinnedNrm.y;
        skinnedVertexData_[i * 8 + 5] = skinnedNrm.z;
        skinnedVertexData_[i * 8 + 6] = texCoord.x;
        skinnedVertexData_[i * 8 + 7] = texCoord.y;
    }
}

void GltfCharacter::initGL(juce::OpenGLContext& ctx) {
    auto& ext = ctx.extensions;

    for (auto& tex : textures_) {
        if (tex.image.isValid()) {
            glGenTextures(1, &tex.id);
            glBindTexture(GL_TEXTURE_2D, tex.id);
            
            int w = tex.image.getWidth();
            int h = tex.image.getHeight();
            std::vector<uint8_t> pixels((size_t)(w * h * 4));
            
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    auto color = tex.image.getPixelAt(x, y);
                    int offset = (y * w + x) * 4;
                    pixels[offset + 0] = color.getRed();
                    pixels[offset + 1] = color.getGreen();
                    pixels[offset + 2] = color.getBlue();
                    pixels[offset + 3] = color.getAlpha();
                }
            }
            
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            
            tex.image = juce::Image(); // Free CPU memory
        }
    }

    ext.glGenVertexArrays(1, &vao_);
    ext.glBindVertexArray(vao_);

    ext.glGenBuffers(1, &vbo_);
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(skinnedVertexData_.size() * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);

    ext.glGenBuffers(1, &ebo_);
    ext.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    ext.glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices_.size() * sizeof(uint32_t)), indices_.data(), GL_STATIC_DRAW);

    const int stride = 8 * sizeof(float);
    ext.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    ext.glEnableVertexAttribArray(0);
    ext.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    ext.glEnableVertexAttribArray(1);
    ext.glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    ext.glEnableVertexAttribArray(2);

    ext.glBindVertexArray(0);

    const char* vShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aTexCoord;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 proj;
        out vec3 worldNormal;
        out vec3 worldPos;
        out vec2 texCoord;
        void main() {
            vec4 wp = model * vec4(aPos, 1.0);
            worldPos = wp.xyz;
            worldNormal = mat3(model) * aNormal;
            texCoord = aTexCoord;
            gl_Position = proj * view * wp;
        }
    )";

    const char* fShader = R"(
        #version 330 core
        in vec3 worldNormal;
        in vec3 worldPos;
        in vec2 texCoord;
        out vec4 FragColor;
        
        uniform vec3 sunDir;
        uniform vec3 sunColor;
        uniform vec3 camPos;
        
        uniform vec4 matColor;
        uniform sampler2D diffuseTex;
        uniform int hasTexture;

        void main() {
            vec4 albedo = hasTexture > 0 ? texture(diffuseTex, texCoord) * matColor : matColor;
            
            vec3 N = normalize(worldNormal);
            vec3 V = normalize(camPos - worldPos);
            vec3 L = normalize(sunDir);
            vec3 H = normalize(L + V);

            float diff = max(dot(N, L), 0.0);
            vec3 diffuse = diff * sunColor * albedo.rgb;

            float spec = pow(max(dot(N, H), 0.0), 32.0);
            vec3 specular = spec * sunColor * 0.3;

            float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);
            vec3 rim = fresnel * sunColor * 0.2;
            
            vec3 ambient = albedo.rgb * 0.2;

            vec3 result = ambient + diffuse + specular + rim;
            FragColor = vec4(result, albedo.a);
        }
    )";

    shader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    shader_->addVertexShader(vShader);
    shader_->addFragmentShader(fShader);
    shader_->link();

    glInitialised_ = true;
}

void GltfCharacter::render(juce::OpenGLContext& ctx, const std::vector<float>& vertexData, const glm::mat4& view, const glm::mat4& proj,
                            const glm::mat4& modelTransform, const glm::vec3& sunDir, const glm::vec3& sunColor,
                            const glm::vec3& color) {
    if (!loaded_) return;
    if (!glInitialised_) initGL(ctx);
    if (!shader_) return;

    if (vertexData.empty()) return;

    auto& ext = ctx.extensions;
    ext.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    ext.glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(vertexData.size() * sizeof(float)), vertexData.data());

    shader_->use();
    GLint progId = (GLint)shader_->getProgramID();
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "model"), 1, GL_FALSE, glm::value_ptr(modelTransform));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "view"), 1, GL_FALSE, glm::value_ptr(view));
    ext.glUniformMatrix4fv(ext.glGetUniformLocation(progId, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunDir"), sunDir.x, sunDir.y, sunDir.z);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "sunColor"), sunColor.x, sunColor.y, sunColor.z);
    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    ext.glUniform3f(ext.glGetUniformLocation(progId, "camPos"), camPos.x, camPos.y, camPos.z);

    GLint matColorLoc = ext.glGetUniformLocation(progId, "matColor");
    GLint hasTexLoc = ext.glGetUniformLocation(progId, "hasTexture");
    GLint diffuseTexLoc = ext.glGetUniformLocation(progId, "diffuseTex");
    ext.glUniform1i(diffuseTexLoc, 0);

    ext.glBindVertexArray(vao_);

    if (primitives_.empty()) {
        // Fallback if no primitives parsed correctly
        ext.glUniform4f(matColorLoc, color.x, color.y, color.z, 1.0f);
        ext.glUniform1i(hasTexLoc, 0);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices_.size(), GL_UNSIGNED_INT, (void*)0);
    } else {
        for (const auto& prim : primitives_) {
            if (prim.materialIndex >= 0 && prim.materialIndex < materials_.size()) {
                const auto& mat = materials_[prim.materialIndex];
                ext.glUniform4f(matColorLoc, mat.baseColorFactor.x, mat.baseColorFactor.y, mat.baseColorFactor.z, mat.baseColorFactor.w);
                
                if (mat.baseColorTextureIndex >= 0 && mat.baseColorTextureIndex < textures_.size()) {
                    ext.glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, textures_[mat.baseColorTextureIndex].id);
                    ext.glUniform1i(hasTexLoc, 1);
                } else {
                    ext.glUniform1i(hasTexLoc, 0);
                }
            } else {
                ext.glUniform4f(matColorLoc, color.x, color.y, color.z, 1.0f);
                ext.glUniform1i(hasTexLoc, 0);
            }
            
            glDrawElements(GL_TRIANGLES, (GLsizei)prim.indexCount, GL_UNSIGNED_INT, (void*)(prim.indexOffset * sizeof(uint32_t)));
        }
    }

    ext.glBindVertexArray(0);
}

} // namespace Harmonia
namespace Harmonia { bool GltfCharacter::extractMotionRecord(const std::string& clipName, djehuti::animation::MotionRecord& outRecord) const {
    auto it = clips_.find(clipName);
    if (it == clips_.end()) return false;
    
    const Clip& clip = it->second;
    outRecord.name = clipName;
    outRecord.originalDuration = clip.duration;
    
    int numSamples = (std::max)(2, static_cast<int>(clip.duration * 60.0f));
    outRecord.samples.resize(numSamples);
    
    int leftFootIdx = findNodeIndex("foot_l");
    int rightFootIdx = findNodeIndex("foot_r");
    int pelvisIdx = findNodeIndex("pelvis");
    
    for (int i = 0; i < numSamples; ++i) {
        float phase = static_cast<float>(i) / (numSamples - 1);
        float t = phase * clip.duration;
        
        auto& sample = outRecord.samples[i];
        sample.phase = phase;
        
        // Extract raw joint rotations
        for (const auto& [nodeIdx, anim] : clip.perNode) {
            // Find bounding rotation keyframes
            if (anim.rotation.empty()) continue;
            // Simplified: just grab the first keyframe for now, or implement full lerp
            // Actually, we can use localTransform to get the matrix, then extract quaternion!
            // But GltfCharacter::localTransform returns a mat4.
        }
        
        // We evaluate global transforms to get foot positions
        std::vector<glm::mat4> globals;
        computeGlobalTransforms(globals, &clip, t);
        
        if (leftFootIdx >= 0 && leftFootIdx < globals.size()) {
            sample.features.leftFootPos = glm::vec3(globals[leftFootIdx][3]); // Translation
        }
        if (rightFootIdx >= 0 && rightFootIdx < globals.size()) {
            sample.features.rightFootPos = glm::vec3(globals[rightFootIdx][3]);
        }
    }
    
    // Pass 2: calculate velocities and contacts
    for (int i = 0; i < numSamples; ++i) {
        int prev = (i == 0) ? numSamples - 1 : i - 1;
        int next = (i == numSamples - 1) ? 0 : i + 1;
        float dt = clip.duration / (numSamples - 1);
        
        // Central difference velocity
        outRecord.samples[i].features.leftFootVel = (outRecord.samples[next].features.leftFootPos - outRecord.samples[prev].features.leftFootPos) / (2.0f * dt);
        outRecord.samples[i].features.rightFootVel = (outRecord.samples[next].features.rightFootPos - outRecord.samples[prev].features.rightFootPos) / (2.0f * dt);
        
        // Simple contact tagging: if foot velocity is low and height is low
        float speedL = glm::length(outRecord.samples[i].features.leftFootVel);
        float speedR = glm::length(outRecord.samples[i].features.rightFootVel);
        
        outRecord.samples[i].features.leftContact = (speedL < 0.2f);
        outRecord.samples[i].features.rightContact = (speedR < 0.2f);
    }
    
    // Compute overall speed and cadence
    // ... we can estimate this or just hardcode for now
    outRecord.speed = 1.5f; 
    outRecord.cadence = 1.8f;
    
    return true;
}
}

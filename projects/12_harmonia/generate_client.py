import os

project_root = r'D:\000 Tech Research\projects\12_harmonia'
files = {
    'Source/Client/Network/HarpFramer.h': '''#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include "../../Shared/Network/Protocol.h"

namespace Harmonia {
namespace Net {

class HarpFramer {
public:
    void feed(const uint8_t* data, int numBytes);
    void setCallback(std::function<void(MsgType, const juce::MemoryBlock&)> cb);
    void reset();

private:
    enum class State { ReadingHeader, ReadingPayload };
    State state_ = State::ReadingHeader;
    juce::MemoryBlock buffer_;
    PacketHeader currentHeader_;
    bool firstPacket_ = true;
    std::function<void(MsgType, const juce::MemoryBlock&)> callback_;
};

} // namespace Net
} // namespace Harmonia
''',
    'Source/Client/Network/HarpFramer.cpp': '''#include "HarpFramer.h"
#include "../../Shared/Network/HarpSerializer.h"

namespace Harmonia {
namespace Net {

void HarpFramer::feed(const uint8_t* data, int numBytes) {
    buffer_.append(data, numBytes);
    
    while (true) {
        if (state_ == State::ReadingHeader) {
            if (buffer_.getSize() < sizeof(PacketHeader)) return;
            
            HarpReader reader(buffer_.getData(), sizeof(PacketHeader));
            currentHeader_.magic = reader.readU32();
            currentHeader_.type = static_cast<MsgType>(reader.readU16());
            currentHeader_.payloadLength = reader.readU32();
            
            if (currentHeader_.magic != kHarmoniaMagic) {
                // Invalid magic, clear buffer
                buffer_.setSize(0);
                return;
            }
            
            buffer_.removeSection(0, sizeof(PacketHeader));
            state_ = State::ReadingPayload;
        }
        
        if (state_ == State::ReadingPayload) {
            if (buffer_.getSize() < currentHeader_.payloadLength) return;
            
            juce::MemoryBlock payload(buffer_.getData(), currentHeader_.payloadLength);
            if (callback_) callback_(currentHeader_.type, payload);
            
            buffer_.removeSection(0, currentHeader_.payloadLength);
            state_ = State::ReadingHeader;
        }
    }
}

void HarpFramer::setCallback(std::function<void(MsgType, const juce::MemoryBlock&)> cb) {
    callback_ = cb;
}

void HarpFramer::reset() {
    buffer_.setSize(0);
    state_ = State::ReadingHeader;
    firstPacket_ = true;
}

}
}
''',
    'Source/Client/Network/NetworkClient.h': '''#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include "HarpFramer.h"

namespace Harmonia {
namespace Net {

class NetworkClient : public juce::Thread {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void onConnected(uint32_t playerID, const juce::String& serverName) = 0;
        virtual void onDisconnected(const juce::String& reason) = 0;
        virtual void onMessage(MsgType type, const juce::MemoryBlock& payload) = 0;
    };
    
    NetworkClient();
    ~NetworkClient() override;
    
    bool connect(const juce::String& host, int port, const juce::String& playerName, const juce::String& session);
    void disconnect();
    void send(MsgType type, const juce::MemoryBlock& payload);
    void run() override;
    void addListener(Listener* l);
    void removeListener(Listener* l);
    
    bool isConnected() const;
    uint32_t localPlayerID() const;
    int pingMs() const;
    
private:
    juce::StreamingSocket socket_;
    HarpFramer framer_;
    juce::ListenerList<Listener> listeners_;
    uint32_t playerID_ = 0;
    std::atomic<bool> connected_{false};
    juce::CriticalSection sendLock_;
    std::atomic<int> pingMs_{0};
};

}
}
''',
    'Source/Client/Network/NetworkClient.cpp': '''#include "NetworkClient.h"
#include "../../Shared/Network/HarpSerializer.h"

namespace Harmonia {
namespace Net {

NetworkClient::NetworkClient() : Thread("NetworkClientThread") {
    framer_.setCallback([this](MsgType type, const juce::MemoryBlock& payload) {
        if (type == MsgType::HandshakeRes) {
            HarpReader reader(payload.getData(), payload.getSize());
            playerID_ = reader.readU32();
            juce::String serverName = reader.readString();
            connected_ = true;
            listeners_.call(&Listener::onConnected, playerID_, serverName);
        } else {
            listeners_.call(&Listener::onMessage, type, payload);
        }
    });
}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const juce::String& host, int port, const juce::String& playerName, const juce::String& session) {
    if (socket_.connect(host, port, 3000)) {
        framer_.reset();
        
        juce::MemoryBlock payload;
        HarpWriter writer(payload);
        writer.writeU32(1); // version
        writer.writeString(playerName);
        writer.writeString(session);
        
        send(MsgType::HandshakeReq, payload);
        startThread();
        return true;
    }
    return false;
}

void NetworkClient::disconnect() {
    signalThreadShouldExit();
    socket_.close();
    stopThread(2000);
    if (connected_) {
        connected_ = false;
        listeners_.call(&Listener::onDisconnected, "Disconnected by user");
    }
}

void NetworkClient::send(MsgType type, const juce::MemoryBlock& payload) {
    juce::ScopedLock lock(sendLock_);
    juce::MemoryBlock packet;
    HarpWriter writer(packet);
    writer.writeU32(kHarmoniaMagic);
    writer.writeU16(static_cast<uint16_t>(type));
    writer.writeU32(static_cast<uint32_t>(payload.getSize()));
    if (payload.getSize() > 0) {
        packet.append(payload.getData(), payload.getSize());
    }
    socket_.write(packet.getData(), static_cast<int>(packet.getSize()));
}

void NetworkClient::run() {
    uint8_t buffer[4096];
    while (!threadShouldExit()) {
        int bytesRead = socket_.read(buffer, sizeof(buffer), true);
        if (bytesRead > 0) {
            framer_.feed(buffer, bytesRead);
        } else if (bytesRead < 0) {
            connected_ = false;
            juce::MessageManager::getInstance()->callAsync([this]() {
                listeners_.call(&Listener::onDisconnected, "Connection lost");
            });
            break;
        }
    }
}

void NetworkClient::addListener(Listener* l) { listeners_.add(l); }
void NetworkClient::removeListener(Listener* l) { listeners_.remove(l); }
bool NetworkClient::isConnected() const { return connected_; }
uint32_t NetworkClient::localPlayerID() const { return playerID_; }
int NetworkClient::pingMs() const { return pingMs_; }

}
}
''',
    'Source/Client/Network/MessageHandler.h': '''#pragma once
#include "NetworkClient.h"
#include "../../Shared/World/WorldState.h"
#include "../Engine/Audio/AudioEngine.h"
#include "../World/OpenWorld.h"

namespace Harmonia {
class MessageHandler : public Net::NetworkClient::Listener {
public:
    MessageHandler(WorldState* ws, AudioEngine* audio, OpenWorld* world);
    void onConnected(uint32_t playerID, const juce::String& serverName) override;
    void onDisconnected(const juce::String& reason) override;
    void onMessage(Net::MsgType type, const juce::MemoryBlock& payload) override;
private:
    WorldState* worldState_;
    AudioEngine* audio_;
    OpenWorld* world_;
};
}
''',
    'Source/Client/Network/MessageHandler.cpp': '''#include "MessageHandler.h"
#include "../../Shared/Network/HarpSerializer.h"

namespace Harmonia {
MessageHandler::MessageHandler(WorldState* ws, AudioEngine* audio, OpenWorld* world)
    : worldState_(ws), audio_(audio), world_(world) {}

void MessageHandler::onConnected(uint32_t playerID, const juce::String& serverName) {}
void MessageHandler::onDisconnected(const juce::String& reason) {}

void MessageHandler::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {
    Net::HarpReader reader(payload.getData(), payload.getSize());
    switch (type) {
        case Net::MsgType::PlayerJoined: {
            uint32_t id = reader.readU32();
            juce::String name = reader.readString();
            float hue = reader.readFloat();
            glm::vec3 pos(reader.readFloat(), reader.readFloat(), reader.readFloat());
            world_->onPlayerJoined(id, name, hue, pos);
            break;
        }
        case Net::MsgType::PlayerLeft: {
            uint32_t id = reader.readU32();
            world_->onPlayerLeft(id);
            break;
        }
        case Net::MsgType::PlayerPosition: {
            uint32_t id = reader.readU32();
            glm::vec3 pos(reader.readFloat(), reader.readFloat(), reader.readFloat());
            float yaw = reader.readFloat();
            world_->onPlayerPosition(id, pos, yaw);
            break;
        }
        case Net::MsgType::NoteOn: {
            uint32_t id = reader.readU32();
            int note = reader.readU8();
            float vel = reader.readFloat();
            world_->onNoteOn(id, note, vel);
            audio_->noteOn(note, vel, 1);
            break;
        }
        case Net::MsgType::NoteOff: {
            uint32_t id = reader.readU32();
            int note = reader.readU8();
            world_->onNoteOff(id, note);
            audio_->noteOff(note, 1);
            break;
        }
        default: break;
    }
}
}
''',
    'Source/Client/Network/StateSyncer.h': '''#pragma once
#include <juce_core/juce_core.h>
#include "../../Shared/World/WorldState.h"

namespace Harmonia {
class StateSyncer {
public:
    StateSyncer(WorldState* ws);
    void applyFullSync(const juce::MemoryBlock& payload);
    void applyVoxelDelta(const juce::MemoryBlock& payload);
private:
    WorldState* worldState_;
};
}
''',
    'Source/Client/Network/StateSyncer.cpp': '''#include "StateSyncer.h"
#include "../../Shared/Network/HarpSerializer.h"

namespace Harmonia {
StateSyncer::StateSyncer(WorldState* ws) : worldState_(ws) {}

void StateSyncer::applyFullSync(const juce::MemoryBlock& payload) {
    // Deserialize voxel grid
}

void StateSyncer::applyVoxelDelta(const juce::MemoryBlock& payload) {
    // Apply delta
}
}
''',
    'Source/Client/Engine/Rendering/Camera.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Harmonia {
class Camera {
public:
    Camera();
    void mouseDown(const juce::MouseEvent&);
    void mouseDrag(const juce::MouseEvent&);
    void mouseWheelMove(const juce::MouseWheelDetails&);
    void update(float dt);
    
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;
    glm::vec3 position() const;
    void flyTo(glm::vec3 target, float distanceFromTarget, float durationSec);
    
    float azimuth = 0.3f;
    float elevation = 0.4f;
    float distance = 40.0f;
    glm::vec3 pivot = {0,0,0};
    
private:
    float targetAzimuth_, targetElevation_, targetDistance_;
    glm::vec3 targetPivot_;
    juce::Point<float> lastMousePos_;
    bool animating_ = false;
    float animT_ = 0.f, animDur_ = 1.f;
    glm::vec3 animStartPos_, animEndTarget_;
    float animStartDist_, animEndDist_;
};
}
''',
    'Source/Client/Engine/Rendering/Camera.cpp': '''#include "Camera.h"
#include <algorithm>

namespace Harmonia {
Camera::Camera() : targetAzimuth_(azimuth), targetElevation_(elevation), targetDistance_(distance), targetPivot_(pivot) {}

void Camera::mouseDown(const juce::MouseEvent& e) {
    lastMousePos_ = e.position;
}

void Camera::mouseDrag(const juce::MouseEvent& e) {
    float dx = (e.position.x - lastMousePos_.x) * 0.01f;
    float dy = (e.position.y - lastMousePos_.y) * 0.01f;
    targetAzimuth_ -= dx;
    targetElevation_ -= dy;
    targetElevation_ = std::clamp(targetElevation_, -1.5f, 1.5f);
    lastMousePos_ = e.position;
}

void Camera::mouseWheelMove(const juce::MouseWheelDetails& e) {
    targetDistance_ -= e.deltaY * 10.0f;
    targetDistance_ = std::max(1.0f, targetDistance_);
}

void Camera::update(float dt) {
    if (animating_) {
        animT_ += dt;
        float t = std::min(1.0f, animT_ / animDur_);
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        pivot = glm::mix(animStartPos_, animEndTarget_, t);
        distance = glm::mix(animStartDist_, animEndDist_, t);
        if (t >= 1.0f) animating_ = false;
        targetPivot_ = pivot;
        targetDistance_ = distance;
    } else {
        azimuth += (targetAzimuth_ - azimuth) * 10.0f * dt;
        elevation += (targetElevation_ - elevation) * 10.0f * dt;
        distance += (targetDistance_ - distance) * 10.0f * dt;
        pivot += (targetPivot_ - pivot) * 10.0f * dt;
    }
}

glm::mat4 Camera::viewMatrix() const {
    glm::vec3 pos = position();
    return glm::lookAt(pos, pivot, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 1000.0f);
}

glm::vec3 Camera::position() const {
    float h = distance * cos(elevation);
    float y = distance * sin(elevation);
    float x = h * sin(azimuth);
    float z = h * cos(azimuth);
    return pivot + glm::vec3(x, y, z);
}

void Camera::flyTo(glm::vec3 target, float distanceFromTarget, float durationSec) {
    animating_ = true;
    animT_ = 0.f;
    animDur_ = durationSec;
    animStartPos_ = pivot;
    animEndTarget_ = target;
    animStartDist_ = distance;
    animEndDist_ = distanceFromTarget;
}
}
''',
    'Source/Client/Engine/Rendering/ShaderLibrary.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>
#include <memory>

namespace Harmonia {
class ShaderLibrary {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    
    juce::OpenGLShaderProgram* voxel() const { return voxelShader_.get(); }
    juce::OpenGLShaderProgram* particle() const { return particleShader_.get(); }
    juce::OpenGLShaderProgram* sweep() const { return sweepShader_.get(); }
    juce::OpenGLShaderProgram* avatar() const { return avatarShader_.get(); }
    juce::OpenGLShaderProgram* starfield() const { return starfieldShader_.get(); }
    juce::OpenGLShaderProgram* bloom() const { return bloomShader_.get(); }
    juce::OpenGLShaderProgram* ground() const { return groundShader_.get(); }
    
private:
    std::unique_ptr<juce::OpenGLShaderProgram> voxelShader_, particleShader_, sweepShader_, avatarShader_, starfieldShader_, bloomShader_, groundShader_;
};
}
''',
    'Source/Client/Engine/Rendering/ShaderLibrary.cpp': '''#include "ShaderLibrary.h"

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
    particleShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    particleShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
    particleShader_->link();
    
    sweepShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    sweepShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    sweepShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
    sweepShader_->link();
    
    avatarShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    avatarShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    avatarShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
    avatarShader_->link();

    starfieldShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    starfieldShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    starfieldShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
    starfieldShader_->link();

    bloomShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    bloomShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    bloomShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
    bloomShader_->link();

    groundShader_ = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    groundShader_->addVertexShader("#version 330 core\\n void main() { gl_Position = vec4(0); }");
    groundShader_->addFragmentShader("#version 330 core\\n out vec4 f; void main() { f = vec4(1); }");
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
''',
    'Source/Client/Engine/Rendering/OpenGLContext.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>
#include "Camera.h"
#include "ShaderLibrary.h"
#include "ParticleSystem.h"
#include "StarField.h"
#include "../../Shared/World/VoxelGrid.h"
#include "../../Shared/World/WorldState.h"

namespace Harmonia {
class VoxelRenderer;

class HarmoniaGLContext : public juce::OpenGLRenderer {
public:
    HarmoniaGLContext();
    ~HarmoniaGLContext() override;
    void attachTo(juce::Component& comp);
    void detach();
    
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    
    void setVoxelGrid(std::shared_ptr<VoxelGrid> grid);
    void setWorldState(WorldState* state);
    Camera& camera();
    
private:
    juce::OpenGLContext glCtx_;
    std::unique_ptr<ShaderLibrary> shaders_;
    std::unique_ptr<VoxelRenderer> voxelRenderer_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<StarField> stars_;
    Camera camera_;
    float sweepZ_ = 0.f;
    double lastRenderTime_ = 0.0;
};
}
''',
    'Source/Client/Engine/Rendering/OpenGLContext.cpp': '''#include "OpenGLContext.h"
#include <juce_core/juce_core.h>

namespace Harmonia {
class VoxelRenderer {
public:
    void render(juce::OpenGLShaderProgram* shader, const glm::mat4& vp) {}
};

HarmoniaGLContext::HarmoniaGLContext() {
    glCtx_.setRenderer(this);
    glCtx_.setContinuousRepainting(true);
}

HarmoniaGLContext::~HarmoniaGLContext() {
    detach();
}

void HarmoniaGLContext::attachTo(juce::Component& comp) {
    glCtx_.attachTo(comp);
}

void HarmoniaGLContext::detach() {
    glCtx_.detach();
}

void HarmoniaGLContext::newOpenGLContextCreated() {
    shaders_ = std::make_unique<ShaderLibrary>();
    shaders_->initialise(glCtx_);
    
    voxelRenderer_ = std::make_unique<VoxelRenderer>();
    particles_ = std::make_unique<ParticleSystem>();
    particles_->initialise(glCtx_);
    
    stars_ = std::make_unique<StarField>();
    stars_->initialise(glCtx_, 2000);
}

void HarmoniaGLContext::renderOpenGL() {
    juce::OpenGLHelpers::clear(juce::Colour(0xff050510));
    
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    float dt = lastRenderTime_ > 0 ? (float)(now - lastRenderTime_) : 0.016f;
    lastRenderTime_ = now;
    
    camera_.update(dt);
    particles_->update(dt);
    
    // Rendering logic placeholder
}

void HarmoniaGLContext::openGLContextClosing() {
    shaders_->shutdown();
    particles_->shutdown();
    stars_->shutdown();
}

void HarmoniaGLContext::setVoxelGrid(std::shared_ptr<VoxelGrid> grid) {}
void HarmoniaGLContext::setWorldState(WorldState* state) {}
Camera& HarmoniaGLContext::camera() { return camera_; }
}
''',
    'Source/Client/Engine/Rendering/ParticleSystem.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>

namespace Harmonia {
struct Particle {
    glm::vec3 pos, vel;
    glm::vec4 colour;
    float life;
    float maxLife;
    float size;
};

class ParticleSystem {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    void update(float dt);
    void render(const glm::mat4& view, const glm::mat4& proj);
    void burst(glm::vec3 position, juce::Colour colour, int count = 24);
private:
    std::vector<Particle> particles_;
    GLuint vbo_ = 0, vao_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
};
}
''',
    'Source/Client/Engine/Rendering/ParticleSystem.cpp': '''#include "ParticleSystem.h"

namespace Harmonia {
void ParticleSystem::initialise(juce::OpenGLContext& ctx) {}
void ParticleSystem::shutdown() {}
void ParticleSystem::update(float dt) {}
void ParticleSystem::render(const glm::mat4& view, const glm::mat4& proj) {}
void ParticleSystem::burst(glm::vec3 position, juce::Colour colour, int count) {}
}
''',
    'Source/Client/Engine/Rendering/AvatarRenderer.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <map>
#include "../../Shared/World/WorldState.h"

namespace Harmonia {
class AvatarRenderer {
public:
    void initialise(juce::OpenGLContext& ctx);
    void shutdown();
    void render(const std::map<uint32_t, PlayerState>& players,
                const glm::mat4& view, const glm::mat4& proj,
                double timeSeconds);
    void noteTriggered(uint32_t playerID);
private:
    GLuint sphereVAO_ = 0, sphereVBO_ = 0, sphereEBO_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
    std::map<uint32_t, float> pulseTimes_;
};
}
''',
    'Source/Client/Engine/Rendering/AvatarRenderer.cpp': '''#include "AvatarRenderer.h"

namespace Harmonia {
void AvatarRenderer::initialise(juce::OpenGLContext& ctx) {}
void AvatarRenderer::shutdown() {}
void AvatarRenderer::render(const std::map<uint32_t, PlayerState>& players, const glm::mat4& view, const glm::mat4& proj, double timeSeconds) {}
void AvatarRenderer::noteTriggered(uint32_t playerID) {}
}
''',
    'Source/Client/Engine/Rendering/StarField.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>

namespace Harmonia {
class StarField {
public:
    void initialise(juce::OpenGLContext& ctx, int numStars = 2000);
    void shutdown();
    void render(const glm::mat4& view, const glm::mat4& proj);
private:
    GLuint vao_ = 0, vbo_ = 0;
    int numStars_ = 0;
    juce::OpenGLShaderProgram* shader_ = nullptr;
};
}
''',
    'Source/Client/Engine/Rendering/StarField.cpp': '''#include "StarField.h"

namespace Harmonia {
void StarField::initialise(juce::OpenGLContext& ctx, int numStars) {}
void StarField::shutdown() {}
void StarField::render(const glm::mat4& view, const glm::mat4& proj) {}
}
''',
    'Source/Client/Engine/Rendering/PostProcess.h': '''#pragma once
#include <juce_opengl/juce_opengl.h>

namespace Harmonia {
class PostProcess {
public:
    void initialise(juce::OpenGLContext& ctx, int width, int height);
    void resize(int width, int height);
    void shutdown();
    
    void beginCapture();
    void endCapture();
    void applyBloom();
private:
    GLuint fbo_ = 0, colorTex_ = 0, pingTex_ = 0, pongTex_ = 0;
    GLuint quadVAO_ = 0, quadVBO_ = 0;
    juce::OpenGLShaderProgram* bloomShader_ = nullptr;
    int width_ = 0, height_ = 0;
};
}
''',
    'Source/Client/Engine/Rendering/PostProcess.cpp': '''#include "PostProcess.h"

namespace Harmonia {
void PostProcess::initialise(juce::OpenGLContext& ctx, int width, int height) {}
void PostProcess::resize(int width, int height) {}
void PostProcess::shutdown() {}
void PostProcess::beginCapture() {}
void PostProcess::endCapture() {}
void PostProcess::applyBloom() {}
}
''',
    'Source/Client/Engine/Audio/AdditiveVoice.h': '''#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace Harmonia {
class AdditiveVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheelPos) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    
    enum class Timbre { Sine, Bell, Organ, String, Glass };
    static void setGlobalTimbre(Timbre t);
    
private:
    double phase_[8] = {};
    double freq_ = 440.0;
    float level_ = 0.f;
    float envelope_ = 0.f;
    bool releasing_ = false;
    
    float attack_ = 0.01f;
    float decay_ = 0.1f;
    float sustain_ = 0.7f;
    float release_ = 0.3f;
    
    static float harmonicAmps_[8];
};
}
''',
    'Source/Client/Engine/Audio/AdditiveVoice.cpp': '''#include "AdditiveVoice.h"

namespace Harmonia {
float AdditiveVoice::harmonicAmps_[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

void AdditiveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) {
    freq_ = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    level_ = velocity;
    envelope_ = 1.0f; // Simplified
    releasing_ = false;
}

void AdditiveVoice::stopNote(float, bool) {
    releasing_ = true;
    clearCurrentNote();
}

void AdditiveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) {
    if (envelope_ <= 0.0f) {
        clearCurrentNote();
        return;
    }
    
    // Additive synthesis simplified rendering
    for (int i = 0; i < numSamples; ++i) {
        float sample = 0;
        for (int h = 0; h < 8; ++h) {
            sample += std::sin(phase_[h]) * harmonicAmps_[h];
            phase_[h] += (freq_ * (h+1)) * 2.0 * juce::MathConstants<double>::pi / getSampleRate();
        }
        
        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) {
            outputBuffer.addSample(ch, startSample + i, sample * level_ * envelope_ * 0.1f);
        }
    }
}

void AdditiveVoice::setGlobalTimbre(Timbre t) {
    // Modify harmonicAmps_ based on preset
}
}
''',
    'Source/Client/Engine/Audio/AudioEngine.h': '''#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AdditiveVoice.h"

namespace Harmonia {
class AudioEngine : public juce::AudioSource {
public:
    AudioEngine();
    ~AudioEngine() override;
    
    bool initialise();
    void shutdown();
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    
    void noteOn(int midiNote, float velocity, int channel);
    void noteOff(int midiNote, int channel);
    
    void setTimbre(AdditiveVoice::Timbre t);
    juce::AudioDeviceManager& deviceManager();
    
private:
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer player_;
    juce::Synthesiser synth_;
    juce::MidiMessageCollector midiCollector_;
};
}
''',
    'Source/Client/Engine/Audio/AudioEngine.cpp': '''#include "AudioEngine.h"

namespace Harmonia {
struct HarmoniaSound : public juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

AudioEngine::AudioEngine() {
    for (int i = 0; i < 16; ++i) synth_.addVoice(new AdditiveVoice());
    synth_.addSound(new HarmoniaSound());
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::initialise() {
    deviceManager_.initialiseWithDefaultDevices(0, 2);
    player_.setSource(this);
    deviceManager_.addAudioCallback(&player_);
    return true;
}

void AudioEngine::shutdown() {
    deviceManager_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
}

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    synth_.setCurrentPlaybackSampleRate(sampleRate);
    midiCollector_.reset(sampleRate);
}

void AudioEngine::releaseResources() {}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    bufferToFill.clearActiveBufferRegion();
    juce::MidiBuffer incomingMidi;
    midiCollector_.removeNextBlockOfMessages(incomingMidi, bufferToFill.numSamples);
    synth_.renderNextBlock(*bufferToFill.buffer, incomingMidi, bufferToFill.startSample, bufferToFill.numSamples);
}

void AudioEngine::noteOn(int midiNote, float velocity, int channel) {
    synth_.noteOn(channel, midiNote, velocity);
}

void AudioEngine::noteOff(int midiNote, int channel) {
    synth_.noteOff(channel, midiNote, 0.0f, true);
}

void AudioEngine::setTimbre(AdditiveVoice::Timbre t) {
    AdditiveVoice::setGlobalTimbre(t);
}

juce::AudioDeviceManager& AudioEngine::deviceManager() { return deviceManager_; }
}
''',
    'Source/Client/Engine/Audio/MidiEngine.h': '''#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace Harmonia {
class MidiEngine {
public:
    MidiEngine();
    ~MidiEngine();
    
    juce::StringArray getAvailableDevices() const;
    bool openDevice(const juce::String& deviceName);
    void closeDevice();
    bool isOpen() const;
    
    void sendNoteOn(int midiNote, int velocity, int channel);
    void sendNoteOff(int midiNote, int channel);
    void sendAllNotesOff();
    void scheduleNoteOff(int midiNote, int channel, double durationMs);
    
private:
    std::unique_ptr<juce::MidiOutput> output_;
    juce::CriticalSection lock_;
};
}
''',
    'Source/Client/Engine/Audio/MidiEngine.cpp': '''#include "MidiEngine.h"

namespace Harmonia {
MidiEngine::MidiEngine() {}
MidiEngine::~MidiEngine() { closeDevice(); }

juce::StringArray MidiEngine::getAvailableDevices() const {
    juce::StringArray devs;
    for (auto d : juce::MidiOutput::getAvailableDevices()) devs.add(d.name);
    return devs;
}

bool MidiEngine::openDevice(const juce::String& deviceName) {
    for (auto d : juce::MidiOutput::getAvailableDevices()) {
        if (d.name == deviceName) {
            output_ = juce::MidiOutput::openDevice(d.identifier);
            return output_ != nullptr;
        }
    }
    return false;
}

void MidiEngine::closeDevice() {
    juce::ScopedLock lock(lock_);
    output_.reset();
}

bool MidiEngine::isOpen() const { return output_ != nullptr; }

void MidiEngine::sendNoteOn(int midiNote, int velocity, int channel) {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::noteOn(channel, midiNote, (uint8_t)velocity));
}

void MidiEngine::sendNoteOff(int midiNote, int channel) {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::noteOff(channel, midiNote, (uint8_t)0));
}

void MidiEngine::sendAllNotesOff() {
    juce::ScopedLock lock(lock_);
    if (output_) output_->sendMessageNow(juce::MidiMessage::allNotesOff(1));
}

void MidiEngine::scheduleNoteOff(int midiNote, int channel, double durationMs) {
    // Stub
}
}
''',
    'Source/Client/Engine/Audio/SpatialAudio.h': '''#pragma once
#include <glm/glm.hpp>
#include <utility>

namespace Harmonia {
class SpatialAudio {
public:
    static std::pair<float,float> panGains(glm::vec3 listener, glm::vec3 source, float listenerYaw, float maxDistance = 50.f);
    static float distanceGain(glm::vec3 listener, glm::vec3 source, float maxDistance = 50.f);
};
}
''',
    'Source/Client/Engine/Audio/SpatialAudio.cpp': '''#include "SpatialAudio.h"

namespace Harmonia {
std::pair<float,float> SpatialAudio::panGains(glm::vec3 listener, glm::vec3 source, float listenerYaw, float maxDistance) {
    return {0.5f, 0.5f}; // Stub
}

float SpatialAudio::distanceGain(glm::vec3 listener, glm::vec3 source, float maxDistance) {
    return 1.0f; // Stub
}
}
''',
    'Source/Client/World/Regions/IRegion.h': '''#pragma once
#include <glm/glm.hpp>
#include <juce_core/juce_core.h>

namespace Harmonia {
class IRegion {
public:
    virtual ~IRegion() = default;
    virtual void enter() = 0;
    virtual void exit() = 0;
    virtual void update(float dt) = 0;
    virtual void render(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual juce::String name() const = 0;
    virtual glm::vec3 worldCenter() const = 0;
    virtual float radius() const = 0;
};
}
''',
    'Source/Client/World/Regions/LivingGridRegion.h': '''#pragma once
#include "IRegion.h"

namespace Harmonia {
class LivingGridRegion : public IRegion {
public:
    void enter() override;
    void exit() override;
    void update(float dt) override;
    void render(const glm::mat4& view, const glm::mat4& proj) override;
    juce::String name() const override { return "Living Grid"; }
    glm::vec3 worldCenter() const override { return {0,0,0}; }
    float radius() const override { return 100.0f; }
};
}
''',
    'Source/Client/World/Regions/LivingGridRegion.cpp': '''#include "LivingGridRegion.h"

namespace Harmonia {
void LivingGridRegion::enter() {}
void LivingGridRegion::exit() {}
void LivingGridRegion::update(float dt) {}
void LivingGridRegion::render(const glm::mat4& view, const glm::mat4& proj) {}
}
''',
    'Source/Client/World/RemotePlayer.h': '''#pragma once
#include <juce_core/juce_core.h>
#include <glm/glm.hpp>
#include <juce_graphics/juce_graphics.h>

namespace Harmonia {
class RemotePlayer {
public:
    RemotePlayer(uint32_t id, const juce::String& name, float colorHue);
    
    void updatePosition(float x, float y, float z, float yaw);
    void noteOn(int midiNote, float velocity);
    void update(float dt);
    
    uint32_t playerID() const;
    juce::String playerName() const;
    glm::vec3 position() const;
    float yaw() const;
    juce::Colour colour() const;
    bool isPulsing() const;
    
private:
    uint32_t id_;
    juce::String name_;
    float colorHue_;
    glm::vec3 targetPos_, currentPos_;
    float targetYaw_, currentYaw_;
    float pulseTime_ = 0.f;
};
}
''',
    'Source/Client/World/RemotePlayer.cpp': '''#include "RemotePlayer.h"

namespace Harmonia {
RemotePlayer::RemotePlayer(uint32_t id, const juce::String& name, float colorHue)
    : id_(id), name_(name), colorHue_(colorHue) {}

void RemotePlayer::updatePosition(float x, float y, float z, float yaw) {
    targetPos_ = {x, y, z};
    targetYaw_ = yaw;
}

void RemotePlayer::noteOn(int midiNote, float velocity) {
    pulseTime_ = 1.0f;
}

void RemotePlayer::update(float dt) {
    currentPos_ += (targetPos_ - currentPos_) * 10.0f * dt;
    currentYaw_ += (targetYaw_ - currentYaw_) * 10.0f * dt;
    if (pulseTime_ > 0.f) pulseTime_ -= dt;
}

uint32_t RemotePlayer::playerID() const { return id_; }
juce::String RemotePlayer::playerName() const { return name_; }
glm::vec3 RemotePlayer::position() const { return currentPos_; }
float RemotePlayer::yaw() const { return currentYaw_; }
juce::Colour RemotePlayer::colour() const { return juce::Colour(colorHue_, 1.0f, 1.0f, 1.0f); }
bool RemotePlayer::isPulsing() const { return pulseTime_ > 0.f; }
}
''',
    'Source/Client/World/PlayerController.h': '''#pragma once
#include <glm/glm.hpp>
#include <set>

namespace Harmonia {
class PlayerController {
public:
    PlayerController();
    void update(float dt, const std::set<int>& keysDown);
    void mouseMove(float dx, float dy);
    
    glm::vec3 position() const;
    float yaw() const;
    bool dirty() const;
    void clearDirty();
    
private:
    glm::vec3 pos_ = {0, 2, 0};
    float yaw_ = 0.f;
    float speed_ = 10.f;
    bool dirty_ = false;
};
}
''',
    'Source/Client/World/PlayerController.cpp': '''#include "PlayerController.h"
#include <cmath>

namespace Harmonia {
PlayerController::PlayerController() {}

void PlayerController::update(float dt, const std::set<int>& keysDown) {
    // Basic WASD logic stub
    dirty_ = true;
}

void PlayerController::mouseMove(float dx, float dy) {
    yaw_ += dx * 0.01f;
    dirty_ = true;
}

glm::vec3 PlayerController::position() const { return pos_; }
float PlayerController::yaw() const { return yaw_; }
bool PlayerController::dirty() const { return dirty_; }
void PlayerController::clearDirty() { dirty_ = false; }
}
''',
    'Source/Client/World/OpenWorld.h': '''#pragma once
#include <map>
#include <set>
#include <vector>
#include <memory>
#include "../Engine/Rendering/Camera.h"
#include "PlayerController.h"
#include "RemotePlayer.h"
#include "Regions/IRegion.h"
#include "../Engine/Rendering/AvatarRenderer.h"
#include "../Engine/Rendering/StarField.h"
#include "../Engine/Rendering/ParticleSystem.h"
#include "../Engine/Rendering/PostProcess.h"
#include "../Engine/Audio/AudioEngine.h"
#include "../Engine/Audio/MidiEngine.h"
#include "../../Shared/World/WorldState.h"

namespace Harmonia {
namespace Net { class NetworkClient; }

class OpenWorld {
public:
    OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi,
              Net::NetworkClient* net, juce::OpenGLContext* ctx);
    
    void update(float dt, const std::set<int>& keysDown, float mouseDx, float mouseDy);
    void render(const glm::mat4& view, const glm::mat4& proj);
    
    void onPlayerJoined(uint32_t id, const juce::String& name, float hue, glm::vec3 pos);
    void onPlayerLeft(uint32_t id);
    void onPlayerPosition(uint32_t id, glm::vec3 pos, float yaw);
    void onNoteOn(uint32_t playerID, int midiNote, float vel);
    void onNoteOff(uint32_t playerID, int midiNote);
    
    Camera& camera();
    PlayerController& localPlayer();
    
private:
    void detectRegion();
    
    WorldState* worldState_;
    AudioEngine* audio_;
    Net::NetworkClient* net_;
    
    PlayerController localPlayer_;
    Camera camera_;
    
    std::map<uint32_t, std::unique_ptr<RemotePlayer>> remotePlayers_;
    std::vector<std::unique_ptr<IRegion>> regions_;
    IRegion* currentRegion_ = nullptr;
    
    std::unique_ptr<AvatarRenderer> avatarRenderer_;
    std::unique_ptr<StarField> starField_;
    std::unique_ptr<ParticleSystem> particles_;
    std::unique_ptr<PostProcess> postProcess_;
};
}
''',
    'Source/Client/World/OpenWorld.cpp': '''#include "OpenWorld.h"

namespace Harmonia {
OpenWorld::OpenWorld(WorldState* state, AudioEngine* audio, MidiEngine* midi, Net::NetworkClient* net, juce::OpenGLContext* ctx)
    : worldState_(state), audio_(audio), net_(net) {}

void OpenWorld::update(float dt, const std::set<int>& keysDown, float mouseDx, float mouseDy) {
    localPlayer_.mouseMove(mouseDx, mouseDy);
    localPlayer_.update(dt, keysDown);
    for (auto& [id, player] : remotePlayers_) player->update(dt);
}

void OpenWorld::render(const glm::mat4& view, const glm::mat4& proj) {
    if (currentRegion_) currentRegion_->render(view, proj);
}

void OpenWorld::onPlayerJoined(uint32_t id, const juce::String& name, float hue, glm::vec3 pos) {
    remotePlayers_[id] = std::make_unique<RemotePlayer>(id, name, hue);
    remotePlayers_[id]->updatePosition(pos.x, pos.y, pos.z, 0.0f);
}

void OpenWorld::onPlayerLeft(uint32_t id) { remotePlayers_.erase(id); }

void OpenWorld::onPlayerPosition(uint32_t id, glm::vec3 pos, float yaw) {
    if (remotePlayers_.count(id)) remotePlayers_[id]->updatePosition(pos.x, pos.y, pos.z, yaw);
}

void OpenWorld::onNoteOn(uint32_t playerID, int midiNote, float vel) {
    if (remotePlayers_.count(playerID)) remotePlayers_[playerID]->noteOn(midiNote, vel);
}

void OpenWorld::onNoteOff(uint32_t playerID, int midiNote) {}

Camera& OpenWorld::camera() { return camera_; }
PlayerController& OpenWorld::localPlayer() { return localPlayer_; }

void OpenWorld::detectRegion() {}
}
''',
    'Source/Client/UI/DesignTokens.h': '''#pragma once
#include <juce_graphics/juce_graphics.h>

namespace Harmonia {
namespace UI {
    inline const juce::Colour kBgDeep     { 0xff050510 };
    inline const juce::Colour kBgMid      { 0xff0d0d2a };
    inline const juce::Colour kAccentCyan { 0xff00e5ff };
    inline const juce::Colour kAccentAmber{ 0xffffb300 };
    inline const juce::Colour kAccentGold { 0xffffd600 };
    inline const juce::Colour kTextPrimary  { 0xfff0f0ff };
    inline const juce::Colour kTextSecondary{ 0xff8888aa };
    
    inline constexpr int kPadSm = 8;
    inline constexpr int kPadMd = 16;
    inline constexpr int kPadLg = 32;
    
    inline juce::Font primaryFont(float size) { return juce::Font("Segoe UI", size, juce::Font::plain); }
    inline juce::Font monoFont(float size) { return juce::Font("Consolas", size, juce::Font::plain); }
}
}
''',
    'Source/Client/Shell/SplashScreen.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace Harmonia {
class SplashScreen : public juce::Component, private juce::Timer {
public:
    SplashScreen();
    void paint(juce::Graphics&) override;
    void resized() override {}
    
    std::function<void()> onComplete;
    
private:
    void timerCallback() override;
    float time_ = 0.f;
    float fadeOut_ = 0.f;
    int harmonic_ = 1;
    bool completing_ = false;
};
}
''',
    'Source/Client/Shell/SplashScreen.cpp': '''#include "SplashScreen.h"
#include "../UI/DesignTokens.h"

namespace Harmonia {
SplashScreen::SplashScreen() {
    startTimerHz(60);
}

void SplashScreen::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgDeep);
    g.setColour(UI::kTextPrimary.withAlpha(1.0f - fadeOut_));
    g.setFont(UI::primaryFont(48.0f));
    g.drawText("H A R M O N I A", getLocalBounds(), juce::Justification::centred);
}

void SplashScreen::timerCallback() {
    time_ += 0.016f;
    if (time_ > 2.0f && !completing_) {
        completing_ = true;
    }
    if (completing_) {
        fadeOut_ += 0.02f;
        if (fadeOut_ >= 1.0f) {
            stopTimer();
            if (onComplete) onComplete();
        }
    }
    repaint();
}
}
''',
    'Source/Client/Shell/ServerBrowser.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace Harmonia {
class ServerBrowser : public juce::Component, private juce::Button::Listener {
public:
    ServerBrowser();
    void resized() override;
    void paint(juce::Graphics&) override;
    
    std::function<void(juce::String host, int port, juce::String playerName, juce::String session)> onConnect;
    std::function<void()> onSolo;
    
private:
    void buttonClicked(juce::Button*) override;
    
    juce::Label titleLabel_;
    juce::TextEditor hostField_, portField_, nameField_, sessionField_;
    juce::TextButton connectBtn_{"Connect"}, soloBtn_{"Solo"};
    juce::Label statusLabel_;
};
}
''',
    'Source/Client/Shell/ServerBrowser.cpp': '''#include "ServerBrowser.h"
#include "../UI/DesignTokens.h"

namespace Harmonia {
ServerBrowser::ServerBrowser() {
    addAndMakeVisible(connectBtn_);
    addAndMakeVisible(soloBtn_);
    connectBtn_.addListener(this);
    soloBtn_.addListener(this);
}

void ServerBrowser::resized() {
    auto b = getLocalBounds().withSizeKeepingCentre(300, 400);
    connectBtn_.setBounds(b.removeFromBottom(40));
    soloBtn_.setBounds(b.removeFromBottom(40).withTrimmedTop(10));
}

void ServerBrowser::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgDeep);
}

void ServerBrowser::buttonClicked(juce::Button* b) {
    if (b == &connectBtn_ && onConnect) {
        onConnect("127.0.0.1", 4440, "Player", "");
    } else if (b == &soloBtn_ && onSolo) {
        onSolo();
    }
}
}
''',
    'Source/Client/Shell/Journal.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace Harmonia {
struct JournalEntry {
    juce::String title;
    juce::String body;
    juce::Colour colour;
    juce::Time discoveredAt;
};

class Journal : public juce::Component {
public:
    Journal();
    void addEntry(const juce::String& title, const juce::String& body, juce::Colour colour);
    void paint(juce::Graphics&) override;
    void resized() override;
    
    void save(const juce::File& f) const;
    void load(const juce::File& f);
    
private:
    std::vector<JournalEntry> entries_;
    juce::Viewport viewport_;
    juce::Component contentComp_;
};
}
''',
    'Source/Client/Shell/Journal.cpp': '''#include "Journal.h"
#include "../UI/DesignTokens.h"

namespace Harmonia {
Journal::Journal() {
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&contentComp_, false);
}

void Journal::addEntry(const juce::String& title, const juce::String& body, juce::Colour colour) {
    entries_.push_back({title, body, colour, juce::Time::getCurrentTime()});
    repaint();
}

void Journal::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgMid.withAlpha(0.8f));
}

void Journal::resized() {
    viewport_.setBounds(getLocalBounds());
}

void Journal::save(const juce::File& f) const {}
void Journal::load(const juce::File& f) {}
}
''',
    'Source/Client/UI/HudOverlay.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonia {
class HudOverlay : public juce::Component {
public:
    HudOverlay();
    void paint(juce::Graphics&) override;
    
    void setCurrentRegion(const juce::String& regionName);
    void setConnectionStatus(bool connected, int pingMs, int playerCount);
    void setCurrentNote(int midiNote, float velocity);
    void showHint(const juce::String& text, int displayMs = 3000);
    
private:
    juce::String regionName_;
    bool connected_ = false;
    int pingMs_ = 0, playerCount_ = 0;
    int currentNote_ = -1;
    juce::String hintText_;
    juce::Time hintExpiry_;
};
}
''',
    'Source/Client/UI/HudOverlay.cpp': '''#include "HudOverlay.h"
#include "DesignTokens.h"

namespace Harmonia {
HudOverlay::HudOverlay() {}

void HudOverlay::paint(juce::Graphics& g) {
    g.setFont(UI::primaryFont(16.f));
    g.setColour(UI::kTextPrimary);
    g.drawText(regionName_, 10, 10, 200, 20, juce::Justification::topLeft);
}

void HudOverlay::setCurrentRegion(const juce::String& regionName) { regionName_ = regionName; repaint(); }
void HudOverlay::setConnectionStatus(bool connected, int pingMs, int playerCount) {}
void HudOverlay::setCurrentNote(int midiNote, float velocity) {}
void HudOverlay::showHint(const juce::String& text, int displayMs) {}
}
''',
    'Source/Client/UI/ChatBox.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>

namespace Harmonia {
class ChatBox : public juce::Component, private juce::Timer {
public:
    ChatBox();
    void paint(juce::Graphics&) override;
    void resized() override;
    void keyPressed(const juce::KeyPress&);
    
    void addMessage(const juce::String& playerName, const juce::String& msg, juce::Colour nameColor);
    std::function<void(juce::String)> onSend;
    
private:
    void timerCallback() override;
    struct ChatLine { juce::String name, text; juce::Colour color; juce::Time time; float alpha = 1.f; };
    std::deque<ChatLine> lines_;
    juce::TextEditor input_;
    bool inputVisible_ = false;
};
}
''',
    'Source/Client/UI/ChatBox.cpp': '''#include "ChatBox.h"

namespace Harmonia {
ChatBox::ChatBox() {
    addAndMakeVisible(input_);
    input_.setVisible(false);
    startTimerHz(10);
}

void ChatBox::paint(juce::Graphics& g) {}

void ChatBox::resized() {
    input_.setBounds(0, getHeight() - 30, getWidth(), 30);
}

void ChatBox::keyPressed(const juce::KeyPress&) {}
void ChatBox::addMessage(const juce::String& playerName, const juce::String& msg, juce::Colour nameColor) {}
void ChatBox::timerCallback() {}
}
''',
    'Source/Client/App/HarmoniaApp.h': '''#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Network/NetworkClient.h"
#include "../Network/MessageHandler.h"
#include "../Network/StateSyncer.h"
#include "../../Shared/World/WorldState.h"
#include "../Engine/Audio/AudioEngine.h"
#include "../Engine/Audio/MidiEngine.h"
#include "../Engine/Rendering/OpenGLContext.h"
#include "../World/OpenWorld.h"
#include "../Shell/SplashScreen.h"
#include "../Shell/ServerBrowser.h"
#include "../Shell/Journal.h"
#include "../UI/HudOverlay.h"
#include "../UI/ChatBox.h"

namespace Harmonia {
class HarmoniaApp : public juce::Component, public Net::NetworkClient::Listener, private juce::KeyListener {
public:
    HarmoniaApp();
    ~HarmoniaApp() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    
    void onConnected(uint32_t playerID, const juce::String& serverName) override;
    void onDisconnected(const juce::String& reason) override;
    void onMessage(Net::MsgType type, const juce::MemoryBlock& payload) override;
    
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    
private:
    void showSplash();
    void showServerBrowser();
    void enterWorld();
    void spawnLocalServer();
    
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<MidiEngine> midi_;
    std::unique_ptr<Net::NetworkClient> net_;
    std::unique_ptr<MessageHandler> msgHandler_;
    std::unique_ptr<StateSyncer> syncer_;
    std::unique_ptr<WorldState> worldState_;
    std::unique_ptr<OpenWorld> world_;
    std::unique_ptr<HarmoniaGLContext> glCtx_;
    
    std::unique_ptr<SplashScreen> splash_;
    std::unique_ptr<ServerBrowser> browser_;
    std::unique_ptr<HudOverlay> hud_;
    std::unique_ptr<ChatBox> chat_;
    std::unique_ptr<Journal> journal_;
    
    std::unique_ptr<juce::ChildProcess> localServer_;
    std::set<int> keysDown_;
};
}
''',
    'Source/Client/App/HarmoniaApp.cpp': '''#include "HarmoniaApp.h"
#include "../UI/DesignTokens.h"

namespace Harmonia {
HarmoniaApp::HarmoniaApp() {
    audio_ = std::make_unique<AudioEngine>();
    audio_->initialise();
    midi_ = std::make_unique<MidiEngine>();
    net_ = std::make_unique<Net::NetworkClient>();
    worldState_ = std::make_unique<WorldState>();
    glCtx_ = std::make_unique<HarmoniaGLContext>();
    glCtx_->attachTo(*this);
    
    world_ = std::make_unique<OpenWorld>(worldState_.get(), audio_.get(), midi_.get(), net_.get(), &glCtx_->glCtx_);
    msgHandler_ = std::make_unique<MessageHandler>(worldState_.get(), audio_.get(), world_.get());
    net_->addListener(this);
    
    showSplash();
    addKeyListener(this);
}

HarmoniaApp::~HarmoniaApp() {
    glCtx_->detach();
    net_->removeListener(this);
}

void HarmoniaApp::resized() {
    if (splash_) splash_->setBounds(getLocalBounds());
    if (browser_) browser_->setBounds(getLocalBounds());
}

void HarmoniaApp::paint(juce::Graphics& g) {
    g.fillAll(UI::kBgDeep);
}

void HarmoniaApp::onConnected(uint32_t playerID, const juce::String& serverName) {
    juce::MessageManager::getInstance()->callAsync([this]() { enterWorld(); });
}

void HarmoniaApp::onDisconnected(const juce::String& reason) {}

void HarmoniaApp::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {}

bool HarmoniaApp::keyPressed(const juce::KeyPress& key, juce::Component*) {
    return false;
}

void HarmoniaApp::showSplash() {
    splash_ = std::make_unique<SplashScreen>();
    addAndMakeVisible(*splash_);
    splash_->onComplete = [this]() {
        splash_.reset();
        showServerBrowser();
    };
}

void HarmoniaApp::showServerBrowser() {
    browser_ = std::make_unique<ServerBrowser>();
    addAndMakeVisible(*browser_);
    browser_->onConnect = [this](juce::String host, int port, juce::String name, juce::String session) {
        net_->connect(host, port, name, session);
    };
    browser_->onSolo = [this]() { spawnLocalServer(); };
}

void HarmoniaApp::enterWorld() {
    browser_.reset();
}

void HarmoniaApp::spawnLocalServer() {}
}
''',
    'Source/Client/App/Main.cpp': '''#include <juce_gui_basics/juce_gui_basics.h>
#include "HarmoniaApp.h"

class HarmoniaApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Harmonia"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    
    void initialise(const juce::String&) override {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
    
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff050510), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new Harmonia::HarmoniaApp(), true);
            setResizable(true, true);
            centreWithSize(1600, 1000);
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    
private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(HarmoniaApplication)
'''
}

for rel_path, content in files.items():
    full_path = os.path.join(project_root, rel_path)
    os.makedirs(os.path.dirname(full_path), exist_ok=True)
    with open(full_path, 'w', encoding='utf-8') as f:
        f.write(content)
print('Done writing client files')

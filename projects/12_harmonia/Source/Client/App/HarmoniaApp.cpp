#include "HarmoniaApp.h"
#include "Client/UI/DesignTokens.h"
#include "Shared/Network/HarpSerializer.h"
#include <thread>

namespace Harmonia {
HarmoniaApp::HarmoniaApp() {
    audio_ = std::make_unique<AudioEngine>();
    audio_->initialise();
    midi_ = std::make_unique<MidiEngine>();
    net_ = std::make_unique<Net::NetworkClient>();
    worldState_ = std::make_unique<WorldState>();
    glCtx_ = std::make_unique<HarmoniaGLContext>();
    glCtx_->attachTo(*this);
    
    world_ = std::make_unique<OpenWorld>(worldState_.get(), audio_.get(), midi_.get(), net_.get(), &glCtx_->glContext());
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
    // We don't fill the background here because the OpenGL context clears it.
    // If we fill here, we paint a solid 2D rectangle OVER the 3D world!
}

void HarmoniaApp::onConnected(uint32_t playerID, const juce::String& serverName) {
    if (msgHandler_) msgHandler_->onConnected(playerID, serverName);
    juce::MessageManager::getInstance()->callAsync([this, playerID]() { 
        enterWorld(); 
    });
}

void HarmoniaApp::onDisconnected(const juce::String& reason) {
    if (msgHandler_) msgHandler_->onDisconnected(reason);
}

void HarmoniaApp::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {
    if (msgHandler_) msgHandler_->onMessage(type, payload);
    
    if (type == Net::MsgType::VoxelFullSync) {
        if (glCtx_ && worldState_) {
            glCtx_->setVoxelGrid(worldState_->livingGrid);
        }
    }
}

void HarmoniaApp::enterWorld() {
    if (browser_) browser_.reset();
    grabKeyboardFocus();
    resized();
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
        // Run connect on a background thread so the "Connecting..." UI can render
        std::thread([this, host, port, name, session]() {
            if (!net_->connect(host, port, name, session)) {
                juce::MessageManager::getInstance()->callAsync([this]() {
                    // Connection failed, restore UI state if needed
                });
            }
        }).detach();
    };
    browser_->onSolo = [this]() { spawnLocalServer(); };
    resized();
}


bool HarmoniaApp::keyPressed(const juce::KeyPress& key, juce::Component*) {
    // Simple piano mapping for testing: z x c v b n m
    int note = -1;
    auto code = key.getKeyCode();
    if (code == 'Z' || code == 'z') note = 60; // C4
    if (code == 'X' || code == 'x') note = 62; // D4
    if (code == 'C' || code == 'c') note = 64; // E4
    if (code == 'V' || code == 'v') note = 65; // F4
    if (code == 'B' || code == 'b') note = 67; // G4
    if (code == 'N' || code == 'n') note = 69; // A4
    if (code == 'M' || code == 'm') note = 71; // B4
    
    if (note != -1) {
        Net::HarpWriter w;
        w.writeU8((uint8_t)note);
        w.writeF32(0.8f); // velocity
        if (net_) net_->send(Net::MsgType::NoteOn, w.getPayload());
        return true;
    }
    return false;
}

void HarmoniaApp::mouseDown(const juce::MouseEvent& e) {
    if (glCtx_) glCtx_->camera().mouseDown(e);
}

void HarmoniaApp::mouseDrag(const juce::MouseEvent& e) {
    if (glCtx_) glCtx_->camera().mouseDrag(e);
}

void HarmoniaApp::spawnLocalServer() {}

} // namespace Harmonia


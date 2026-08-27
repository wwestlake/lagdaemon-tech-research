#include "HarmoniaApp.h"
#include "Client/UI/DesignTokens.h"

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

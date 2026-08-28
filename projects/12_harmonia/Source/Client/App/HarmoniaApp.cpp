#include "HarmoniaApp.h"
#include "Client/UI/DesignTokens.h"
#include "Shared/Network/HarpSerializer.h"
#include <thread>

namespace Harmonia {
HarmoniaApp::HarmoniaApp() {
    juce::File logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("harmonia_client.log");
    juce::Logger::setCurrentLogger(juce::FileLogger::createDateStampedLogger(logFile.getParentDirectory().getFullPathName(), "harmonia", ".log", "Harmonia Client"));
    
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
    setWantsKeyboardFocus(true);
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
                    if (browser_) browser_->setConnectionStatusText("Connection failed.");
                });
            }
        }).detach();
    };
    browser_->onSolo = [this]() { spawnLocalServer(); };
    resized();
}


bool HarmoniaApp::keyPressed(const juce::KeyPress&, juce::Component*) { return false; }

bool HarmoniaApp::keyStateChanged(bool /*isKeyDown*/, juce::Component*) {
    // Simple piano mapping for testing: z x c v b n m
    std::map<int, int> keyToNote = {
        {'Z', 60}, {'X', 62}, {'C', 64}, {'V', 65},
        {'B', 67}, {'N', 69}, {'M', 71}
    };
    
    std::set<int> newKeysDown;
    for (auto& pair : keyToNote) {
        if (juce::KeyPress::isKeyCurrentlyDown(pair.first) || juce::KeyPress::isKeyCurrentlyDown(tolower(pair.first))) {
            newKeysDown.insert(pair.second);
        }
    }
    
    // Check for new notes (NoteOn)
    for (int note : newKeysDown) {
        if (keysDown_.find(note) == keysDown_.end()) {
            Net::HarpWriter w;
            w.writeU8((uint8_t)note);
            w.writeF32(0.8f);
            if (net_) net_->send(Net::MsgType::NoteOn, w.getPayload());
            juce::Logger::writeToLog("App: Sent NoteOn " + juce::String(note));
        }
    }
    
    // Check for released notes (NoteOff)
    for (int note : keysDown_) {
        if (newKeysDown.find(note) == newKeysDown.end()) {
            Net::HarpWriter w;
            w.writeU8((uint8_t)note);
            if (net_) net_->send(Net::MsgType::NoteOff, w.getPayload());
            juce::Logger::writeToLog("App: Sent NoteOff " + juce::String(note));
        }
    }
    
    keysDown_ = newKeysDown;
    return false;
}

void HarmoniaApp::mouseDown(const juce::MouseEvent& e) {
    if (glCtx_) glCtx_->camera().mouseDown(e);
}

void HarmoniaApp::mouseDrag(const juce::MouseEvent& e) {
    if (glCtx_) glCtx_->camera().mouseDrag(e);
}

void HarmoniaApp::spawnLocalServer() {
    juce::File exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("HarmoniaServer.exe");
    if (exe.existsAsFile()) {
        exe.startAsProcess();
        juce::Timer::callAfterDelay(1000, [this]() {
            if (browser_) browser_->setConnectionStatusText("Connecting to local server...");
            if (browser_) browser_->onConnect("127.0.0.1", 4440, "Traveller", "main");
        });
    }
}

} // namespace Harmonia


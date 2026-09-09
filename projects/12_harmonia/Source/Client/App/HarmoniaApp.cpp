#include "HarmoniaApp.h"
#include "Client/UI/DesignTokens.h"
#include "Shared/Network/HarpSerializer.h"
#include <thread>

namespace Harmonia {
HarmoniaApp::HarmoniaApp() {
    juce::File logDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("logs");
    logDir.createDirectory();
    juce::File logFile = logDir.getChildFile("harmonia_client.log");
    juce::Logger::setCurrentLogger(juce::FileLogger::createDateStampedLogger(logFile.getParentDirectory().getFullPathName(), "harmonia", ".log", "Harmonia Client"));
    
    audio_ = std::make_unique<AudioEngine>();
    audio_->initialise();
    midi_ = std::make_unique<MidiEngine>();
    net_ = std::make_unique<Net::NetworkClient>();
    worldState_ = std::make_unique<WorldState>();
    glCtx_ = std::make_unique<HarmoniaGLContext>();
    glCtx_->attachTo(*this);
    
    world_ = std::make_unique<OpenWorld>(worldState_.get(), audio_.get(), midi_.get(), net_.get(), glCtx_->camera());
    glCtx_->setOpenWorld(world_.get());
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
    if (browser_)
        browser_->setStatus("Cannot reach server: " + reason, true);
}

void HarmoniaApp::onMessage(Net::MsgType type, const juce::MemoryBlock& payload) {
    if (msgHandler_) msgHandler_->onMessage(type, payload);
}

void HarmoniaApp::enterWorld() {
    if (browser_) browser_.reset();
    grabKeyboardFocus();
    resized();

    // Captured mouse for real game look, now that we're past the connect
    // menu (which needs a normal visible, clickable cursor for its text
    // fields/buttons).
    setMouseCaptured(true);
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
                    if (browser_)
                        browser_->setStatus("Cannot reach server. Check the address and port.", true);
                });
            }
        }).detach();
    };
    browser_->onSolo = [this]() { spawnLocalServer(); };
    resized();
}


bool HarmoniaApp::keyPressed(const juce::KeyPress& key, juce::Component*) {
    if (key == juce::KeyPress::escapeKey) {
        setMouseCaptured(!mouseCaptured_);
        return true;
    }
    return false;
}

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

void HarmoniaApp::mouseDown(const juce::MouseEvent&) {
    // Click-drag orbit is intentionally NOT the default control scheme -
    // continuous mouseMove below (industry-standard FPS/third-person
    // look, no button required) is. Camera::mouseDown/mouseDrag still
    // exist for a possible future special-case mode (e.g. photo mode),
    // just not wired here.
}

void HarmoniaApp::mouseDrag(const juce::MouseEvent&) {
}

void HarmoniaApp::mouseMove(const juce::MouseEvent& e) {
    if (!glCtx_ || !mouseCaptured_) return;

    // Standard "warp to centre" game mouse capture: measure the delta
    // from the window's centre (not from wherever the last event left
    // off), then snap the OS cursor back to centre so it can never run
    // out of screen and every subsequent move measures cleanly again.
    // This never touches Camera directly - it only accumulates into the
    // atomic handoff, which the GL thread alone drains and applies.
    juce::Point<float> centre((float)getWidth() * 0.5f, (float)getHeight() * 0.5f);
    auto delta = e.position - centre;
    if (delta.x != 0.0f || delta.y != 0.0f) {
        glCtx_->addMouseDelta(delta.x, delta.y);
        auto globalCentre = localPointToGlobal(centre.toInt());
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(globalCentre.toFloat());
    }
}

void HarmoniaApp::setMouseCaptured(bool captured) {
    mouseCaptured_ = captured;
    if (captured) {
        juce::Point<float> centre((float)getWidth() * 0.5f, (float)getHeight() * 0.5f);
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(localPointToGlobal(centre.toInt()).toFloat());
        setMouseCursor(juce::MouseCursor::NoCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void HarmoniaApp::spawnLocalServer() {
    const juce::File clientExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::File clientDir = clientExe.getParentDirectory();
    juce::File serverExe = clientDir.getChildFile("HarmoniaServer.exe");

    // CMake keeps the client and server in sibling artefact directories.
    if (!serverExe.existsAsFile())
        serverExe = clientDir.getParentDirectory()
                        .getSiblingFile("HarmoniaServer_artefacts")
                        .getChildFile("Debug")
                        .getChildFile("HarmoniaServer.exe");

    if (!serverExe.existsAsFile()) {
        if (browser_)
            browser_->setStatus("Local server executable was not found.", true);
        return;
    }

    // ChildProcess (unlike File::startAsProcess()) launches with
    // CREATE_NO_WINDOW on Windows - the server console window no longer
    // pops up over the game and steals focus/input.
    localServer_ = std::make_unique<juce::ChildProcess>();
    // The StringArray overload (not the raw command-line String one)
    // quotes each argument itself - needed since this path has spaces
    // ("...\000 Tech Research\..."), which a bare unquoted command-line
    // string would fail to parse correctly.
    if (!localServer_->start(juce::StringArray(serverExe.getFullPathName()))) {
        if (browser_)
            browser_->setStatus("Could not start the local server.", true);
        localServer_.reset();
        return;
    }

    juce::Timer::callAfterDelay(1000, [this]() {
        if (browser_) browser_->setStatus("Connecting to local server...");
        if (browser_) browser_->onConnect("127.0.0.1", 4440, "Traveller", "main");
    });
}

} // namespace Harmonia


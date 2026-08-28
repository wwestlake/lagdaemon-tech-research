#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Client/Network/NetworkClient.h"
#include "Client/Network/MessageHandler.h"
#include "Client/Network/StateSyncer.h"
#include "Shared/World/WorldState.h"
#include "Client/Engine/Audio/AudioEngine.h"
#include "Client/Engine/Audio/MidiEngine.h"
#include "Client/Engine/Rendering/OpenGLContext.h"
#include "Client/World/OpenWorld.h"
#include "Client/Shell/SplashScreen.h"
#include "Client/Shell/ServerBrowser.h"
#include "Client/Shell/Journal.h"
#include "Client/UI/HudOverlay.h"
#include "Client/UI/ChatBox.h"

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
    
    // Input
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    
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

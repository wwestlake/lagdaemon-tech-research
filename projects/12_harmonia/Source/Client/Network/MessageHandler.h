#pragma once
#include "NetworkClient.h"
#include "Shared/World/WorldState.h"
#include "Client/Engine/Audio/AudioEngine.h"
#include "Client/World/OpenWorld.h"

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

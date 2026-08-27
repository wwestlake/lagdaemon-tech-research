#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
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

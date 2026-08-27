#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <atomic>
#include "Shared/Network/Protocol.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia { namespace Server {

class ClientConnection : public juce::Thread {
public:
    ClientConnection(juce::StreamingSocket* socket, uint32_t playerID, 
                     std::function<void(uint32_t, ::Harmonia::Net::MsgType, const juce::MemoryBlock&)> onMessage,
                     std::function<void(uint32_t)> onDisconnect);
    ~ClientConnection() override;
    
    void run() override;
    void send(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload);
    void sendHandshakeResponse(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload);
    void disconnect();
    
    uint32_t     playerID() const;
    juce::String remoteAddress() const;
    bool         isAlive() const;
    int64_t      lastPingMs() const;
    void         recordPong(int64_t timestamp);
    
private:
    std::unique_ptr<juce::StreamingSocket> socket_;
    uint32_t     playerID_;
    std::atomic<bool> alive_;
    std::atomic<int64_t> lastPongMs_;
    juce::CriticalSection sendLock_;
    std::function<void(uint32_t, ::Harmonia::Net::MsgType, const juce::MemoryBlock&)> onMessage_;
    std::function<void(uint32_t)> onDisconnect_;
};

} // namespace
} // namespace Harmonia


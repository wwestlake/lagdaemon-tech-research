#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include "ClientConnection.h"
#include "Shared/World/WorldState.h"

namespace Harmonia { namespace Server {

struct Session {
    juce::String                                  name;
    ::Harmonia::WorldState                   world;
    std::map<uint32_t, std::unique_ptr<ClientConnection>> clients;
    juce::CriticalSection                         lock;
    
    void broadcast(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload, uint32_t excludePlayerID = 0);
    void broadcastToAll(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload);
};

class SessionManager {
public:
    Session* getOrCreateSession(const juce::String& name);
    Session* findSession(const juce::String& name);
    void     removeSession(const juce::String& name);
    void     addClientToSession(const juce::String& sessionName, 
                                 uint32_t playerID,
                                 std::unique_ptr<ClientConnection> conn);
    void     removeClient(uint32_t playerID);
    int      totalPlayerCount() const;

    juce::CriticalSection& getLock() { return lock_; }
    std::map<juce::String, std::unique_ptr<Session>>& getSessions() { return sessions_; }
    
private:
    std::map<juce::String, std::unique_ptr<Session>> sessions_;
    juce::CriticalSection lock_;
};

} // namespace
} // namespace Harmonia


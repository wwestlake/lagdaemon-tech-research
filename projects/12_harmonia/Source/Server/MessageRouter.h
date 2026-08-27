#pragma once

#include <juce_core/juce_core.h>
#include "SessionManager.h"
#include "Shared/Network/Protocol.h"

namespace Harmonia { namespace Server {

class MessageRouter {
public:
    MessageRouter(SessionManager& sessions);
    
    void onMessage(uint32_t playerID, ::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload);
    
private:
    void handlePlayerPosition(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleNoteOn(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleNoteOff(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleVoxelSeedRequest(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleCAParamChange(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleChordNoteAdd(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleChordNoteRemove(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleChordClear(uint32_t playerID, const juce::MemoryBlock& payload);
    void handlePing(uint32_t playerID, const juce::MemoryBlock& payload);
    void handleChat(uint32_t playerID, const juce::MemoryBlock& payload);
    
    Session* findSessionForPlayer(uint32_t playerID);

    SessionManager& sessions_;
};

} // namespace
} // namespace Harmonia


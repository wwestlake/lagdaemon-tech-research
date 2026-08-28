#include "MessageRouter.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

MessageRouter::MessageRouter(SessionManager& sessions) : sessions_(sessions) {}

Session* MessageRouter::findSessionForPlayer(uint32_t playerID) {
    juce::ScopedLock sl(sessions_.getLock());
    for (auto& pair : sessions_.getSessions()) {
        Session* session = pair.second.get();
        juce::ScopedLock sessionLock(session->lock);
        if (session->clients.find(playerID) != session->clients.end()) {
            return session;
        }
    }
    return nullptr;
}

void MessageRouter::onMessage(uint32_t playerID, ::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload) {
    switch (type) {
        case ::Harmonia::Net::MsgType::PlayerPosition:    handlePlayerPosition(playerID, payload); break;
        case ::Harmonia::Net::MsgType::NoteOn:             handleNoteOn(playerID, payload); break;
        case ::Harmonia::Net::MsgType::NoteOff:            handleNoteOff(playerID, payload); break;
        case ::Harmonia::Net::MsgType::VoxelSeedRequest:   handleVoxelSeedRequest(playerID, payload); break;
        case ::Harmonia::Net::MsgType::CAParamChange:      handleCAParamChange(playerID, payload); break;
        case ::Harmonia::Net::MsgType::ChordNoteAdd:       handleChordNoteAdd(playerID, payload); break;
        case ::Harmonia::Net::MsgType::ChordNoteRemove:    handleChordNoteRemove(playerID, payload); break;
        case ::Harmonia::Net::MsgType::ChordClear:         handleChordClear(playerID, payload); break;
        case ::Harmonia::Net::MsgType::Ping:               handlePing(playerID, payload); break;
        case ::Harmonia::Net::MsgType::PlayerChat:         handleChat(playerID, payload); break;
        default: break;
    }
}

void MessageRouter::handlePlayerPosition(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) {
        Net::HarpWriter w;
        w.writeU32(playerID);
        w.writeRaw(payload.getData(), payload.getSize());
        session->broadcast(::Harmonia::Net::MsgType::PlayerPosition, w.getPayload(), playerID);
    }
}

void MessageRouter::handleNoteOn(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) {
        Net::HarpWriter w;
        w.writeU32(playerID);
        w.writeRaw(payload.getData(), payload.getSize());
        session->broadcast(::Harmonia::Net::MsgType::NoteOn, w.getPayload(), playerID);
        
        // Let NoteOn revive dead voxels at the grid center
        if (session->world.livingGrid) {
            juce::ScopedLock sl(session->world.lock);
            int gw = session->world.livingGrid->width();
            int gh = session->world.livingGrid->height();
            int gd = session->world.livingGrid->depth();
            
            // Read note from payload
            Net::HarpReader reader(payload);
            int note = reader.readU8();
            float vel = reader.readF32();
            
            int pc = note % 12;
            int cx = (gw/2) + (pc - 6);
            int cy = gh / 2;
            int cz = gd / 2;
            
            // Spawn a cluster
            session->world.livingGrid->setVoxel(cx, cy, cz, 1.0f);
            session->world.livingGrid->setVoxel(cx+1, cy, cz, 0.8f);
            session->world.livingGrid->setVoxel(cx-1, cy, cz, 0.8f);
        }
    }
}

void MessageRouter::handleNoteOff(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) {
        Net::HarpWriter w;
        w.writeU32(playerID);
        w.writeRaw(payload.getData(), payload.getSize());
        session->broadcast(::Harmonia::Net::MsgType::NoteOff, w.getPayload(), playerID);
    }
}



void MessageRouter::handleVoxelSeedRequest(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) session->broadcast(::Harmonia::Net::MsgType::VoxelDelta, payload);
}

void MessageRouter::handleCAParamChange(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) session->broadcast(::Harmonia::Net::MsgType::CAParamChange, payload);
}

void MessageRouter::handleChordNoteAdd(uint32_t playerID, const juce::MemoryBlock& payload) {}
void MessageRouter::handleChordNoteRemove(uint32_t playerID, const juce::MemoryBlock& payload) {}
void MessageRouter::handleChordClear(uint32_t playerID, const juce::MemoryBlock& payload) {}

void MessageRouter::handlePing(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) {
        juce::ScopedLock sessionLock(session->lock);
        if (auto it = session->clients.find(playerID); it != session->clients.end()) {
            it->second->recordPong(juce::Time::currentTimeMillis());
            it->second->send(::Harmonia::Net::MsgType::Pong, juce::MemoryBlock());
        }
    }
}

void MessageRouter::handleChat(uint32_t playerID, const juce::MemoryBlock& payload) {
    Session* session = findSessionForPlayer(playerID);
    if (session) session->broadcast(::Harmonia::Net::MsgType::PlayerChat, payload);
}

}
} // namespace Harmonia


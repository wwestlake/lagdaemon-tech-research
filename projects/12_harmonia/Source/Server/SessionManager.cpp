#include "SessionManager.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

void Session::broadcast(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload, uint32_t excludePlayerID) {
    juce::ScopedLock sl(lock);
    for (auto& pair : clients) {
        if (pair.first != excludePlayerID && pair.second->isAlive()) {
            pair.second->send(type, payload);
        }
    }
}

void Session::broadcastToAll(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload) {
    broadcast(type, payload, 0);
}

Session* SessionManager::getOrCreateSession(const juce::String& name) {
    juce::ScopedLock sl(lock_);
    auto it = sessions_.find(name);
    if (it != sessions_.end()) {
        return it->second.get();
    }
    
    auto newSession = std::make_unique<Session>();
    newSession->name = name;
    Session* ptr = newSession.get();
    sessions_[name] = std::move(newSession);
    Logger::info("Created new session: " + name);
    return ptr;
}

Session* SessionManager::findSession(const juce::String& name) {
    juce::ScopedLock sl(lock_);
    auto it = sessions_.find(name);
    if (it != sessions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SessionManager::removeSession(const juce::String& name) {
    juce::ScopedLock sl(lock_);
    sessions_.erase(name);
    Logger::info("Removed session: " + name);
}

void SessionManager::addClientToSession(const juce::String& sessionName, uint32_t playerID, std::unique_ptr<ClientConnection> conn) {
    juce::ScopedLock sl(lock_);
    Session* session = getOrCreateSession(sessionName);
    
    juce::ScopedLock sessionLock(session->lock);
    session->clients[playerID] = std::move(conn);
    Logger::info("Added player " + juce::String(playerID) + " to session " + sessionName);
}

void SessionManager::removeClient(uint32_t playerID) {
    juce::ScopedLock sl(lock_);
    for (auto& sessionPair : sessions_) {
        Session& session = *sessionPair.second;
        juce::ScopedLock sessionLock(session.lock);
        if (session.clients.find(playerID) != session.clients.end()) {
            session.clients.erase(playerID);
            Logger::info("Removed player " + juce::String(playerID) + " from session " + session.name);
            break;
        }
    }
}

int SessionManager::totalPlayerCount() const {
    juce::ScopedLock sl(lock_);
    int count = 0;
    for (const auto& sessionPair : sessions_) {
        juce::ScopedLock sessionLock(sessionPair.second->lock);
        count += sessionPair.second->clients.size();
    }
    return count;
}

}
} // namespace Harmonia


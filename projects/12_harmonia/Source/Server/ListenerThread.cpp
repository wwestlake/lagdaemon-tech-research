#include "ListenerThread.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

ListenerThread::ListenerThread(int port, SessionManager& sessions, MessageRouter& router)
    : juce::Thread("Listener"), port_(port), sessions_(sessions), router_(router), running_(false)
{
}

ListenerThread::~ListenerThread() {
    stop();
}

void ListenerThread::stop() {
    running_ = false;
    listener_.close();
    stopThread(2000);
}

void ListenerThread::run() {
    if (!listener_.createListener(port_)) {
        Logger::error("Failed to start listener on port " + juce::String(port_));
        return;
    }
    
    Logger::info("Listener started on port " + juce::String(port_));
    running_ = true;

    while (!threadShouldExit() && running_) {
        juce::StreamingSocket* newSocket = listener_.waitForNextConnection();
        if (newSocket != nullptr) {
            acceptConnection(newSocket);
        }
    }
}

void ListenerThread::acceptConnection(juce::StreamingSocket* newSocket) {
    uint32_t playerID;
    {
        juce::ScopedLock sl(idLock_);
        playerID = nextPlayerID_++;
    }

    auto onMessage = [this](uint32_t id, ::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload) {
        router_.onMessage(id, type, payload);
    };

    auto onDisconnect = [this](uint32_t id) {
        sessions_.removeClient(id);
    };

    auto conn = std::make_unique<ClientConnection>(newSocket, playerID, onMessage, onDisconnect);
    
    juce::String defaultSessionName = "main";
    Session* session = sessions_.getOrCreateSession(defaultSessionName);
    
    performHandshake(*conn, *session);
    
    sessions_.addClientToSession(defaultSessionName, playerID, std::move(conn));
}

void ListenerThread::performHandshake(ClientConnection& conn, Session& session) {
    juce::MemoryBlock welcomePayload; 
    conn.sendHandshakeResponse(::Harmonia::Net::MsgType::Welcome, welcomePayload);
    
    juce::MemoryBlock joinedPayload;
    session.broadcast(::Harmonia::Net::MsgType::PlayerJoined, joinedPayload, conn.playerID());
}

}
} // namespace Harmonia


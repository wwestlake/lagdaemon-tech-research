#include "ListenerThread.h"
#include "Logger.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia { namespace Server {

// Grid dimensions for the main world
static constexpr int kGridW = 24;  // 2 octaves × 12 pitch classes
static constexpr int kGridH = 8;   // 8 octaves
static constexpr int kGridD = 16;  // 16 beat slots

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

    Logger::info("Listening on TCP port " + juce::String(port_) + " (A440)");
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

    Logger::info("Accepted connection from " + newSocket->getHostName()
                 + " → playerID=" + juce::String(playerID));

    auto onMessage = [this](uint32_t id, ::Harmonia::Net::MsgType type,
                            const juce::MemoryBlock& payload) {
        router_.onMessage(id, type, payload);
    };

    auto onDisconnect = [this](uint32_t id) {
        Logger::info("Player " + juce::String(id) + " disconnected");
        // Broadcast PlayerLeft before removing
        auto& allSessions = sessions_.getSessions();
        for (auto& pair : allSessions) {
            juce::ScopedLock sl(pair.second->lock);
            if (pair.second->clients.count(id)) {
                Net::HarpWriter w;
                w.writeU32(id);
                pair.second->broadcast(Net::MsgType::PlayerLeft, w.getPayload());
                break;
            }
        }
        sessions_.removeClient(id);
    };

    auto conn = std::make_unique<ClientConnection>(newSocket, playerID,
                                                   onMessage, onDisconnect);

    // Perform HARP handshake before adding to session
    juce::String sessionName = "main";
    Session* session = sessions_.getOrCreateSession(sessionName);

    // Ensure the world grid exists and is seeded
    {
        juce::ScopedLock sl(session->lock);
        if (!session->world.livingGrid) {
            session->world.livingGrid = std::make_shared<VoxelGrid>(kGridW, kGridH, kGridD);
            session->world.livingGrid->seedRandom(0.12f);  // ~12% sparse seed
            session->world.generation = 0;
            Logger::info("Seeded world grid " + juce::String(kGridW) + "×"
                        + juce::String(kGridH) + "×" + juce::String(kGridD));
        }
    }

    if (!performHandshake(*conn, *session, playerID, sessionName)) {
        Logger::warn("Handshake failed for playerID=" + juce::String(playerID));
        return;
    }

    // Start the receive thread AFTER handshake completes
    conn->startThread();

    // Add to session and notify others
    {
        Net::HarpWriter joinMsg;
        joinMsg.writeU32(playerID);
        joinMsg.writeString("Player" + juce::String(playerID));
        joinMsg.writeF32(((float)(playerID % 12)) / 12.f);  // hue from pitch class
        joinMsg.writeF32(0.f); joinMsg.writeF32(0.f); joinMsg.writeF32(0.f);  // spawn pos
        juce::ScopedLock sl(session->lock);
        session->broadcast(Net::MsgType::PlayerJoined, joinMsg.getPayload());
    }

    sessions_.addClientToSession(sessionName, playerID, std::move(conn));
    Logger::info("Player " + juce::String(playerID) + " joined session \"" + sessionName + "\"");
}

bool ListenerThread::performHandshake(ClientConnection& conn, Session& session,
                                      uint32_t playerID,
                                      const juce::String& sessionName) {
    // ── 1. Read Hello (handshake packet) ─────────────────────────────────────
    Net::HandshakeHeader helloHdr{};
    if (!Net::HarpReader::readHandshakeHeader(*conn.socket(), helloHdr)) {
        Logger::warn("Failed to read Hello header");
        return false;
    }

    if (helloHdr.magic != Net::kHarpMagic) {
        Logger::warn("Bad magic in Hello: " + juce::String::toHexString((int)helloHdr.magic));
        return false;
    }

    if (static_cast<Net::MsgType>(helloHdr.msgType) != Net::MsgType::Hello) {
        Logger::warn("Expected Hello, got msgType=" + juce::String(helloHdr.msgType));
        return false;
    }

    juce::MemoryBlock helloPayload;
    if (!Net::HarpReader::readPayload(*conn.socket(), helloHdr.payloadLen, helloPayload)) {
        Logger::warn("Failed to read Hello payload");
        return false;
    }

    Net::HarpReader helloReader(helloPayload);
    juce::String playerName = helloReader.readString();
    juce::String requestedSession = helloReader.readString();
    // uint16 version not used yet

    Logger::info("Hello from \"" + playerName + "\" → session=\"" + requestedSession + "\"");

    // ── 2. Send Welcome ──────────────────────────────────────────────────────
    {
        Net::HarpWriter welcome;
        welcome.writeU32(playerID);
        welcome.writeString("Harmonia Server v0.1.0");
        welcome.writeString(sessionName);
        welcome.writeU16(Net::kProtocolVersion);
        if (!welcome.sendHandshake(*conn.socket(), Net::MsgType::Welcome)) {
            Logger::warn("Failed to send Welcome");
            return false;
        }
    }

    // ── 3. Send VoxelFullSync ────────────────────────────────────────────────
    {
        juce::ScopedLock sl(session.lock);
        if (session.world.livingGrid) {
            juce::MemoryBlock syncData = session.world.livingGrid->serialiseFull();
            Net::HarpWriter syncMsg;
            // Prefix with dimensions
            syncMsg.writeU8((uint8_t)session.world.livingGrid->width());
            syncMsg.writeU8((uint8_t)session.world.livingGrid->height());
            syncMsg.writeU8((uint8_t)session.world.livingGrid->depth());
            // Append raw voxel data
            const uint8_t* raw = static_cast<const uint8_t*>(syncData.getData());
            for (size_t i = 0; i < syncData.getSize(); ++i)
                syncMsg.writeU8(raw[i]);

            if (!syncMsg.sendPacket(*conn.socket(), Net::MsgType::VoxelFullSync)) {
                Logger::warn("Failed to send VoxelFullSync");
                return false;
            }
        }
    }

    return true;
}

} // namespace Server
} // namespace Harmonia

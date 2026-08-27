#include "NetworkClient.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {
namespace Net {

NetworkClient::NetworkClient() : juce::Thread("NetworkClientThread") {
    framer_.setCallback([this](MsgType type, const juce::MemoryBlock& payload) {
        // Welcome is the server's response to our Hello handshake
        if (type == MsgType::Welcome) {
            HarpReader reader(payload);
            playerID_        = reader.readU32();
            juce::String serverName    = reader.readString();
            juce::String sessionName   = reader.readString();
            connected_       = true;
            juce::MessageManager::getInstance()->callAsync([this, serverName]() {
                listeners_.call(&Listener::onConnected, playerID_, serverName);
            });
        } else if (type == MsgType::Pong) {
            HarpReader reader(payload);
            int64_t t = (int64_t)reader.readU64();
            pingMs_   = (int)(juce::Time::currentTimeMillis() - t);
        } else {
            juce::MessageManager::getInstance()->callAsync([this, type, payload]() {
                listeners_.call(&Listener::onMessage, type, payload);
            });
        }
    });
}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const juce::String& host, int port,
                            const juce::String& playerName,
                            const juce::String& session) {
    if (!socket_.connect(host, port, 3000))
        return false;

    framer_.reset();

    // Build Hello payload: playerName(str) + sessionName(str)
    HarpWriter writer;
    writer.writeString(playerName);
    writer.writeString(session);
    writer.writeU16(kProtocolVersion);  // client version

    // Send handshake (includes HARP magic header)
    if (!writer.sendHandshake(socket_, MsgType::Hello)) {
        socket_.close();
        return false;
    }

    startThread();
    return true;
}

void NetworkClient::disconnect() {
    signalThreadShouldExit();
    socket_.close();
    stopThread(2000);
    if (connected_.exchange(false)) {
        juce::MessageManager::getInstance()->callAsync([this]() {
            listeners_.call(&Listener::onDisconnected, "Disconnected by user");
        });
    }
}

void NetworkClient::send(MsgType type, const juce::MemoryBlock& payload) {
    if (!connected_) return;
    juce::ScopedLock lock(sendLock_);
    HarpWriter writer;
    // Copy payload into writer
    const uint8_t* data = static_cast<const uint8_t*>(payload.getData());
    for (size_t i = 0; i < payload.getSize(); ++i)
        writer.writeU8(data[i]);
    writer.sendPacket(socket_, type);
}

void NetworkClient::run() {
    uint8_t buffer[4096];
    while (!threadShouldExit()) {
        int ready = socket_.waitUntilReady(true, 100);
        if (ready < 0) break;
        if (ready == 0) continue;

        int bytesRead = socket_.read(buffer, sizeof(buffer), false);
        if (bytesRead > 0) {
            framer_.feed(buffer, bytesRead);
        } else if (bytesRead < 0) {
            connected_ = false;
            juce::MessageManager::getInstance()->callAsync([this]() {
                listeners_.call(&Listener::onDisconnected, "Connection lost");
            });
            break;
        }
    }
}

void NetworkClient::addListener(Listener* l)    { listeners_.add(l); }
void NetworkClient::removeListener(Listener* l) { listeners_.remove(l); }
bool NetworkClient::isConnected() const          { return connected_; }
uint32_t NetworkClient::localPlayerID() const    { return playerID_; }
int NetworkClient::pingMs() const                { return pingMs_; }

} // namespace Net
} // namespace Harmonia

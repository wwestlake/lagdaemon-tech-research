#include "ClientConnection.h"
#include "Logger.h"

namespace Harmonia { namespace Server {

ClientConnection::ClientConnection(juce::StreamingSocket* socket, uint32_t playerID, 
                                   std::function<void(uint32_t, ::Harmonia::Net::MsgType, const juce::MemoryBlock&)> onMessage,
                                   std::function<void(uint32_t)> onDisconnect)
    : juce::Thread("ClientConnection_" + juce::String(playerID)),
      socket_(socket),
      playerID_(playerID),
      alive_(true),
      lastPongMs_(juce::Time::currentTimeMillis()),
      onMessage_(std::move(onMessage)),
      onDisconnect_(std::move(onDisconnect))
{
    
}

ClientConnection::~ClientConnection() {
    disconnect();
    stopThread(2000);
}

void ClientConnection::run() {
    while (!threadShouldExit() && alive_) {
        int ready = socket_->waitUntilReady(true, 100);
        if (ready < 0) {
            Logger::error("Socket error on client " + juce::String(playerID_));
            disconnect();
            break;
        } else if (ready == 0) {
            continue; // timeout, loop again
        }

        ::Harmonia::Net::MsgType msgType;
        uint32_t payloadLen = 0;

        Net::PacketHeader hdr{};
            int r = socket_->read(&hdr, (int)sizeof(hdr), true);
            if (r < (int)sizeof(hdr)) { disconnect(); break; }

            msgType    = static_cast<::Harmonia::Net::MsgType>(hdr.msgType);
            payloadLen = hdr.payloadLen;

        juce::MemoryBlock payload;
        if (payloadLen > 0) {
            if (payloadLen > Net::kMaxPayloadBytes) {
                Logger::warn("Payload too large (" + juce::String(payloadLen) + "b) from client " + juce::String(playerID_));
                disconnect();
                break;
            }
            payload.setSize(payloadLen);
            int r = socket_->read(payload.getData(), (int)payloadLen, true);
            if (r < (int)payloadLen) { disconnect(); break; }
        }

        if (onMessage_)
            onMessage_(playerID_, msgType, payload);
    }

    if (onDisconnect_)
        onDisconnect_(playerID_);
}

void ClientConnection::send(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload) {
    if (!alive_) return;

    // Normal packet header: [1b version][2b type][4b len]
    Net::PacketHeader hdr{};
    hdr.version    = Net::kProtocolVersion;
    hdr.msgType    = static_cast<uint16_t>(type);
    hdr.payloadLen = static_cast<uint32_t>(payload.getSize());

    juce::ScopedLock sl(sendLock_);
    socket_->write(&hdr, sizeof(hdr));
    if (hdr.payloadLen > 0)
        socket_->write(payload.getData(), (int)hdr.payloadLen);
}

void ClientConnection::sendHandshakeResponse(::Harmonia::Net::MsgType type, const juce::MemoryBlock& payload) {
    if (!alive_) return;

    // Handshake response from server uses the normal 7-byte header
    // (only the client's first packet carries the HARP magic)
    send(type, payload);
}


void ClientConnection::disconnect() {
    bool expected = true;
    if (alive_.compare_exchange_strong(expected, false)) {
        if (socket_) socket_->close();
    }
}

uint32_t ClientConnection::playerID() const {
    return playerID_;
}

juce::String ClientConnection::remoteAddress() const {
    if (socket_) return socket_->getHostName();
    return "Unknown";
}

bool ClientConnection::isAlive() const {
    return alive_;
}

int64_t ClientConnection::lastPingMs() const {
    return lastPongMs_;
}

void ClientConnection::recordPong(int64_t timestamp) {
    lastPongMs_ = timestamp;
}

}
} // namespace Harmonia



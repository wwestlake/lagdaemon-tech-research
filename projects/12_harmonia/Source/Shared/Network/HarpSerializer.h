#pragma once

#include <juce_core/juce_core.h>
#include "Shared/Network/Protocol.h"

namespace Harmonia { namespace Net {

class HarpWriter {
public:
    HarpWriter() = default;
    ~HarpWriter() = default;

    void writeU8(uint8_t val);
    void writeU16(uint16_t val);
    void writeU32(uint32_t val);
    void writeU64(uint64_t val);
    void writeF32(float val);
    void writeString(const juce::String& str);

    bool sendPacket(juce::StreamingSocket& socket, MsgType type);
    bool sendHandshake(juce::StreamingSocket& socket, MsgType type);

    void clear();
    const juce::MemoryBlock& getPayload() const { return payload; }

private:
    juce::MemoryBlock payload;
    juce::CriticalSection lock;
};

class HarpReader {
public:
    static bool readPacketHeader(juce::StreamingSocket& socket, PacketHeader& out);
    static bool readHandshakeHeader(juce::StreamingSocket& socket, HandshakeHeader& out);
    static bool readPayload(juce::StreamingSocket& socket, uint32_t len, juce::MemoryBlock& out);

    explicit HarpReader(const juce::MemoryBlock& data);

    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    float readF32();
    juce::String readString();

    bool isExhausted() const;

private:
    const juce::MemoryBlock& block;
    size_t offset = 0;
};

} // namespace Net
} // namespace Harmonia

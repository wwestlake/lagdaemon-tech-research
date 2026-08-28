#include "HarpSerializer.h"
#include <cstring>

namespace Harmonia { namespace Net {

// ─────────────────────────────────────────────────────────────────────────────
// HarpWriter
// ─────────────────────────────────────────────────────────────────────────────

void HarpWriter::writeU8(uint8_t val) {
    juce::ScopedLock sl(lock);
    payload.append(&val, sizeof(val));
}

void HarpWriter::writeU16(uint16_t val) {
    juce::ScopedLock sl(lock);
    uint16_t le = juce::ByteOrder::swapIfBigEndian(val);
    payload.append(&le, sizeof(le));
}

void HarpWriter::writeU32(uint32_t val) {
    juce::ScopedLock sl(lock);
    uint32_t le = juce::ByteOrder::swapIfBigEndian(val);
    payload.append(&le, sizeof(le));
}

void HarpWriter::writeU64(uint64_t val) {
    juce::ScopedLock sl(lock);
    uint64_t le = juce::ByteOrder::swapIfBigEndian(val);
    payload.append(&le, sizeof(le));
}

void HarpWriter::writeF32(float val) {
    juce::ScopedLock sl(lock);
    // Assuming IEEE 754 and native little-endian for simplicity. 
    // Float endianness conversion can be tricky, but memcpy handles the bytes.
    uint32_t bits;
    std::memcpy(&bits, &val, sizeof(float));
    bits = juce::ByteOrder::swapIfBigEndian(bits);
    payload.append(&bits, sizeof(bits));
}

void HarpWriter::writeString(const juce::String& str) {
    juce::ScopedLock sl(lock);
    auto utf8 = str.toUTF8();
    size_t len = utf8.sizeInBytes() - 1; // excluding null terminator
    
    // Write length as U16, then bytes
    // For large strings, cap or warn, but uint16 limits to 64k anyway.
    writeU16(static_cast<uint16_t>(len));
    if (len > 0) {
        payload.append(utf8.getAddress(), len);
    }
}

bool HarpWriter::sendPacket(juce::StreamingSocket& socket, MsgType type) {
    juce::ScopedLock sl(lock);
    
    PacketHeader hdr;
    hdr.version = kProtocolVersion;
    hdr.msgType = juce::ByteOrder::swapIfBigEndian(static_cast<uint16_t>(type));
    hdr.payloadLen = juce::ByteOrder::swapIfBigEndian(static_cast<uint32_t>(payload.getSize()));

    int headerWritten = socket.write(reinterpret_cast<const char*>(&hdr), kPacketHeaderSize);
    if (headerWritten != kPacketHeaderSize) return false;

    if (payload.getSize() > 0) {
        int payloadWritten = socket.write(reinterpret_cast<const char*>(payload.getData()), static_cast<int>(payload.getSize()));
        if (payloadWritten != static_cast<int>(payload.getSize())) return false;
    }

    clear();
    return true;
}

bool HarpWriter::sendHandshake(juce::StreamingSocket& socket, MsgType type) {
    juce::ScopedLock sl(lock);
    
    HandshakeHeader hdr;
    hdr.magic = juce::ByteOrder::swapIfBigEndian(kHarpMagic);
    hdr.version = kProtocolVersion;
    hdr.msgType = juce::ByteOrder::swapIfBigEndian(static_cast<uint16_t>(type));
    hdr.payloadLen = juce::ByteOrder::swapIfBigEndian(static_cast<uint32_t>(payload.getSize()));

    int headerWritten = socket.write(reinterpret_cast<const char*>(&hdr), kHandshakeHeaderSize);
    if (headerWritten != kHandshakeHeaderSize) return false;

    if (payload.getSize() > 0) {
        int payloadWritten = socket.write(reinterpret_cast<const char*>(payload.getData()), static_cast<int>(payload.getSize()));
        if (payloadWritten != static_cast<int>(payload.getSize())) return false;
    }

    clear();
    return true;
}

void HarpWriter::clear() {
    // Doesn't need lock if called internally from locked methods, but just in case:
    payload.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// HarpReader
// ─────────────────────────────────────────────────────────────────────────────

bool HarpReader::readPacketHeader(juce::StreamingSocket& socket, PacketHeader& out) {
    char buf[kPacketHeaderSize];
    int bytesRead = socket.read(buf, kPacketHeaderSize, true); // true = block until full length read
    if (bytesRead != kPacketHeaderSize) return false;

    std::memcpy(&out, buf, kPacketHeaderSize);
    out.msgType = juce::ByteOrder::swapIfBigEndian(out.msgType);
    out.payloadLen = juce::ByteOrder::swapIfBigEndian(out.payloadLen);
    return true;
}

bool HarpReader::readHandshakeHeader(juce::StreamingSocket& socket, HandshakeHeader& out) {
    char buf[kHandshakeHeaderSize];
    int bytesRead = socket.read(buf, kHandshakeHeaderSize, true);
    if (bytesRead != kHandshakeHeaderSize) return false;

    std::memcpy(&out, buf, kHandshakeHeaderSize);
    out.magic = juce::ByteOrder::swapIfBigEndian(out.magic);
    out.msgType = juce::ByteOrder::swapIfBigEndian(out.msgType);
    out.payloadLen = juce::ByteOrder::swapIfBigEndian(out.payloadLen);
    return true;
}

bool HarpReader::readPayload(juce::StreamingSocket& socket, uint32_t len, juce::MemoryBlock& out) {
    if (len == 0) {
        out.reset();
        return true;
    }
    
    if (len > kMaxPayloadBytes) {
        return false; // Safety check
    }

    out.setSize(len, false);
    int bytesRead = socket.read(out.getData(), static_cast<int>(len), true);
    return bytesRead == static_cast<int>(len);
}

HarpReader::HarpReader(const juce::MemoryBlock& data) : block(data), offset(0) {}

uint8_t HarpReader::readU8() {
    if (offset + sizeof(uint8_t) > block.getSize()) return 0;
    uint8_t val;
    std::memcpy(&val, static_cast<const char*>(block.getData()) + offset, sizeof(val));
    offset += sizeof(val);
    return val;
}

uint16_t HarpReader::readU16() {
    if (offset + sizeof(uint16_t) > block.getSize()) return 0;
    uint16_t val;
    std::memcpy(&val, static_cast<const char*>(block.getData()) + offset, sizeof(val));
    offset += sizeof(val);
    return juce::ByteOrder::swapIfBigEndian(val);
}

uint32_t HarpReader::readU32() {
    if (offset + sizeof(uint32_t) > block.getSize()) return 0;
    uint32_t val;
    std::memcpy(&val, static_cast<const char*>(block.getData()) + offset, sizeof(val));
    offset += sizeof(val);
    return juce::ByteOrder::swapIfBigEndian(val);
}

uint64_t HarpReader::readU64() {
    if (offset + sizeof(uint64_t) > block.getSize()) return 0;
    uint64_t val;
    std::memcpy(&val, static_cast<const char*>(block.getData()) + offset, sizeof(val));
    offset += sizeof(val);
    return juce::ByteOrder::swapIfBigEndian(val);
}

float HarpReader::readF32() {
    if (offset + sizeof(float) > block.getSize()) return 0.0f;
    uint32_t bits;
    std::memcpy(&bits, static_cast<const char*>(block.getData()) + offset, sizeof(bits));
    offset += sizeof(bits);
    bits = juce::ByteOrder::swapIfBigEndian(bits);
    float val;
    std::memcpy(&val, &bits, sizeof(float));
    return val;
}

juce::String HarpReader::readString() {
    uint16_t len = readU16();
    if (len == 0 || offset + len > block.getSize()) return juce::String();

    juce::String str(juce::CharPointer_UTF8(static_cast<const char*>(block.getData()) + offset),
                     juce::CharPointer_UTF8(static_cast<const char*>(block.getData()) + offset + len));
    offset += len;
    return str;
}

bool HarpReader::isExhausted() const {
    return offset >= block.getSize();
}

void HarpWriter::writeRaw(const void* data, size_t size) {
    if (size > 0) payload.append(data, size);
}

} // namespace Net
} // namespace Harmonia


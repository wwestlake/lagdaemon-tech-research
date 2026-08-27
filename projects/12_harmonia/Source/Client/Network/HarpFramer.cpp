#include "HarpFramer.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {
namespace Net {

void HarpFramer::feed(const uint8_t* data, int numBytes) {
    buffer_.append(data, (size_t)numBytes);

    while (true) {
        if (state_ == State::ReadingHeader) {
            // Decide whether to expect a handshake (11 bytes) or normal (7 bytes) header
            const size_t hdrSize = firstPacket_
                                    ? sizeof(HandshakeHeader)
                                    : sizeof(PacketHeader);

            if (buffer_.getSize() < hdrSize) return;

            if (firstPacket_) {
                // Read the 11-byte handshake header
                HandshakeHeader hdr{};
                std::memcpy(&hdr, buffer_.getData(), sizeof(hdr));
                if (hdr.magic != kHarpMagic) {
                    buffer_.setSize(0);
                    return;
                }
                currentHeader_.version    = hdr.version;
                currentHeader_.msgType    = hdr.msgType;
                currentHeader_.payloadLen = hdr.payloadLen;
                firstPacket_ = false;
            } else {
                // Normal 7-byte header
                std::memcpy(&currentHeader_, buffer_.getData(), sizeof(currentHeader_));
            }

            buffer_.removeSection(0, hdrSize);
            state_ = State::ReadingPayload;
        }

        if (state_ == State::ReadingPayload) {
            const size_t needed = (size_t)currentHeader_.payloadLen;
            if (buffer_.getSize() < needed) return;

            juce::MemoryBlock payload(buffer_.getData(), needed);
            if (callback_)
                callback_(static_cast<MsgType>(currentHeader_.msgType), payload);

            buffer_.removeSection(0, needed);
            state_ = State::ReadingHeader;
        }
    }
}

void HarpFramer::setCallback(std::function<void(MsgType, const juce::MemoryBlock&)> cb) {
    callback_ = std::move(cb);
}

void HarpFramer::reset() {
    buffer_.setSize(0);
    state_ = State::ReadingHeader;
    firstPacket_ = true;
}

} // namespace Net
} // namespace Harmonia

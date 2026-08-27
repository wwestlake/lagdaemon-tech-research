#include "HarpFramer.h"
#include "Shared/Network/HarpSerializer.h"

namespace Harmonia {
namespace Net {

void HarpFramer::feed(const uint8_t* data, int numBytes) {
    buffer_.append(data, numBytes);
    
    while (true) {
        if (state_ == State::ReadingHeader) {
            if (buffer_.getSize() < sizeof(PacketHeader)) return;
            
            HarpReader reader(buffer_.getData(), sizeof(PacketHeader));
            currentHeader_.magic = reader.readU32();
            currentHeader_.type = static_cast<MsgType>(reader.readU16());
            currentHeader_.payloadLength = reader.readU32();
            
            if (currentHeader_.magic != kHarmoniaMagic) {
                // Invalid magic, clear buffer
                buffer_.setSize(0);
                return;
            }
            
            buffer_.removeSection(0, sizeof(PacketHeader));
            state_ = State::ReadingPayload;
        }
        
        if (state_ == State::ReadingPayload) {
            if (buffer_.getSize() < currentHeader_.payloadLength) return;
            
            juce::MemoryBlock payload(buffer_.getData(), currentHeader_.payloadLength);
            if (callback_) callback_(currentHeader_.type, payload);
            
            buffer_.removeSection(0, currentHeader_.payloadLength);
            state_ = State::ReadingHeader;
        }
    }
}

void HarpFramer::setCallback(std::function<void(MsgType, const juce::MemoryBlock&)> cb) {
    callback_ = cb;
}

void HarpFramer::reset() {
    buffer_.setSize(0);
    state_ = State::ReadingHeader;
    firstPacket_ = true;
}

}
}

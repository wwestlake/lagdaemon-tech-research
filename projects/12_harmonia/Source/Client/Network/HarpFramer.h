#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include "Shared/Network/Protocol.h"

namespace Harmonia {
namespace Net {

class HarpFramer {
public:
    void feed(const uint8_t* data, int numBytes);
    void setCallback(std::function<void(MsgType, const juce::MemoryBlock&)> cb);
    void reset();

private:
    enum class State { ReadingHeader, ReadingPayload };
    State state_ = State::ReadingHeader;
    juce::MemoryBlock buffer_;
    PacketHeader currentHeader_;
    bool firstPacket_ = true;
    std::function<void(MsgType, const juce::MemoryBlock&)> callback_;
};

} // namespace Net
} // namespace Harmonia

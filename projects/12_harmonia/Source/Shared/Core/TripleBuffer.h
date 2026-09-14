#pragma once
#include <atomic>
#include <cstdint>

namespace Harmonia {

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() : newFrameReady_(false) {
        buffers_[0] = T();
        buffers_[1] = T();
        buffers_[2] = T();
    }

    TripleBuffer(const T& initial) : newFrameReady_(false) {
        buffers_[0] = initial;
        buffers_[1] = initial;
        buffers_[2] = initial;
    }

    // Called by Producer (Game Thread)
    T& getWriteBuffer() {
        return buffers_[writeIndex_];
    }

    void commitWrite() {
        // Swap writeIndex with idleIndex
        uint8_t oldIdle = idleIndex_.exchange(writeIndex_, std::memory_order_acq_rel);
        writeIndex_ = oldIdle;
        newFrameReady_.store(true, std::memory_order_release);
    }

    // Called by Consumer (Render Thread)
    bool updateReadBuffer() {
        if (newFrameReady_.exchange(false, std::memory_order_acquire)) {
            // Swap readIndex with idleIndex
            uint8_t oldIdle = idleIndex_.exchange(readIndex_, std::memory_order_acq_rel);
            readIndex_ = oldIdle;
            return true;
        }
        return false;
    }

    const T& getReadBuffer() const {
        return buffers_[readIndex_];
    }

private:
    T buffers_[3];
    uint8_t writeIndex_ = 0;
    uint8_t readIndex_ = 1;
    std::atomic<uint8_t> idleIndex_{2};
    std::atomic<bool> newFrameReady_;
};

}

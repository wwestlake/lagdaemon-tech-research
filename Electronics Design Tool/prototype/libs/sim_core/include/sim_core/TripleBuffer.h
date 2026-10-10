#pragma once

// Latest-value hand-off from the scheduler thread to one reader thread
// (decision F5): the writer never waits and never blocks; the reader gets the
// newest complete value. A slow reader simply skips intermediate values.

#include <atomic>
#include <cstdint>

namespace sim
{
template <typename T>
class TripleBuffer
{
public:
    // Writer side (scheduler thread).
    T& back() { return slots[backIndex]; }
    void publish()
    {
        const auto previous = middle.exchange((std::uint8_t)(backIndex | freshBit), std::memory_order_acq_rel);
        backIndex = (std::uint8_t)(previous & indexMask);
        published.fetch_add(1, std::memory_order_relaxed);
    }

    // Reader side (UI, renderer, instruments). True when a newer value arrived.
    bool update()
    {
        if ((middle.load(std::memory_order_acquire) & freshBit) == 0)
            return false;
        const auto previous = middle.exchange(frontIndex, std::memory_order_acq_rel);
        frontIndex = (std::uint8_t)(previous & indexMask);
        return true;
    }
    const T& front() const { return slots[frontIndex]; }
    std::uint64_t publishedCount() const { return published.load(std::memory_order_relaxed); }

private:
    static constexpr std::uint8_t freshBit = 0x4;
    static constexpr std::uint8_t indexMask = 0x3;
    T slots[3] {};
    std::uint8_t backIndex = 0;
    std::uint8_t frontIndex = 1;
    std::atomic<std::uint8_t> middle { 2 };
    std::atomic<std::uint64_t> published { 0 };
};
}

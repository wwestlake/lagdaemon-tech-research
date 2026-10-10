#pragma once

// Simulation time (decision F1): a signed 64-bit count of 1 ps ticks.
//
// Range: times run from 0 to kMaxTick - 1 = 2^63 - 2 ps, about 106.7 days.
// kNever (= kMaxTick) means "no time" (no next event) and can never be
// scheduled. All tick arithmetic in the simulator goes through the checked
// helpers below; a result outside the range is reported, never wrapped.
//
// Same-time ordering uses superdense time: (tick, microstep). Events that
// cascade at one tick advance the microstep, not the tick.

#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace sim
{
using Tick = std::int64_t;

inline constexpr Tick kTicksPerSecond = 1'000'000'000'000LL;
inline constexpr Tick kMaxTick = std::numeric_limits<Tick>::max();
inline constexpr Tick kNever = kMaxTick;          // "no time"; not schedulable
inline constexpr Tick kLastSchedulable = kMaxTick - 1;

// a + b, false if the result would leave [INT64_MIN, kLastSchedulable].
inline bool checkedAdd(Tick a, Tick b, Tick& out)
{
    if (b > 0 && a > kLastSchedulable - b) return false;
    if (b < 0 && a < std::numeric_limits<Tick>::min() - b) return false;
    out = a + b;
    return true;
}

// a * n for n >= 0, false on overflow past kLastSchedulable.
inline bool checkedMultiply(Tick a, std::int64_t n, Tick& out)
{
    if (a < 0 || n < 0) return false;
    if (a != 0 && n > kLastSchedulable / a) return false;
    out = a * n;
    return true;
}

// Seconds to the nearest tick; false if not finite or out of range.
inline bool secondsToTicks(double seconds, Tick& out)
{
    if (!std::isfinite(seconds)) return false;
    const double ticks = seconds * (double)kTicksPerSecond;
    if (ticks >= 9.2e18 || ticks <= -9.2e18) return false; // inside ±(2^63 - margin)
    out = (Tick)std::llround(ticks);
    return out <= kLastSchedulable;
}

// For display and arithmetic on physical quantities (exact below 2^53 ps,
// about 2.5 hours; beyond that the double rounds, the tick never does).
inline double ticksToSeconds(Tick t) { return (double)t / (double)kTicksPerSecond; }

inline Tick ticksFromMicroseconds(std::int64_t us) { return us * 1'000'000LL; }
inline Tick ticksFromMilliseconds(std::int64_t ms) { return ms * 1'000'000'000LL; }

struct SimTime
{
    Tick tick = 0;
    std::uint32_t microstep = 0;
    auto operator<=>(const SimTime&) const = default;
};

inline std::string describeTick(Tick t)
{
    if (t == kNever) return "never";
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.12g s", ticksToSeconds(t));
    return buffer;
}
}

#pragma once

// Per-participant random numbers (decision F6): each participant owns one
// generator seeded from its ParticipantConfig::seed (recorded in the
// manifest). No global generator exists. The state is four integers, so it
// goes into the participant's saved state for checkpoints and replay.
// xoshiro256** seeded through SplitMix64; bit-identical on every build.

#include <array>
#include <cstdint>

namespace sim
{
class Random
{
public:
    explicit Random(std::uint64_t seed = 0) { reseed(seed); }

    void reseed(std::uint64_t seed)
    {
        for (auto& word : s)
        {
            seed += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            word = z ^ (z >> 31);
        }
    }

    std::uint64_t next()
    {
        const std::uint64_t result = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    // Uniform in [0, 1) with 53 random bits.
    double uniform() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }

    std::array<std::uint64_t, 4> state() const { return s; }
    void setState(const std::array<std::uint64_t, 4>& state) { s = state; }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::array<std::uint64_t, 4> s {};
};
}

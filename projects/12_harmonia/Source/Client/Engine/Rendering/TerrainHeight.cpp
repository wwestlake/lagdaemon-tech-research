#include "TerrainHeight.h"
#include <cmath>

namespace Harmonia {

namespace {

// Self-contained hash-based value noise (no external noise library) - a
// classic bilinear-interpolated lattice noise, smootherstepped so it has
// no visible grid artifacts. Deterministic: same (x, z) always gives the
// same height, on any machine, with no seed state to sync over the network.
float hash01(int x, int y) {
    int n = x * 374761393 + y * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    n = n ^ (n >> 16);
    return (n & 0x7fffffff) / float(0x7fffffff);
}

float smootherstep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float valueNoise(float x, float y) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float sx = smootherstep(x - (float)x0), sy = smootherstep(y - (float)y0);
    float n00 = hash01(x0, y0), n10 = hash01(x1, y0);
    float n01 = hash01(x0, y1), n11 = hash01(x1, y1);
    float nx0 = n00 + (n10 - n00) * sx;
    float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy; // [0, 1]
}

// Fractal Brownian motion: several octaves of the noise above, each
// higher-frequency layer contributing less - gives natural-looking,
// non-repetitive variation instead of one smooth sine-like bump.
float fbm(float x, float y, int octaves, float lacunarity, float gain) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += (valueNoise(x * freq, y * freq) * 2.0f - 1.0f) * amp; // [-1, 1] contribution
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f; // ~[-1, 1]
}

float smoothstepEdge(float e0, float e1, float x) {
    // Manual clamp, not std::min/max - windows.h's own min/max macros
    // (pulled in elsewhere in this project for GL_) mangle std::min/max
    // call syntax, so this file avoids them defensively too.
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kMaxHillHeight = 18.0f; // real peak height where hills occur - still gentle relative to the world scale

} // namespace

// Large-scale "roughness mask" (low frequency) decides WHERE hills happen;
// smaller-scale fbm decides their shape. A continuous multiplicative bias
// (e.g. pow()) was tried first and measured (via a standalone numeric
// check, not just eyeballing screenshots) to compound with fbm's own
// sub-theoretical-max practical range - the tallest point on the WHOLE map
// came out under 1 unit, invisible everywhere. smoothstep here instead
// creates genuine near-0 (flat) or near-1 (full hill amplitude) regions
// with a smooth transition between them, so hilly patches actually reach
// close to kMaxHillHeight rather than being uniformly crushed down.
float TerrainHeight::heightAt(float worldX, float worldZ) {
    const float maskFreq = 1.0f / 150.0f;
    float maskRaw = fbm(worldX * maskFreq, worldZ * maskFreq, 3, 2.0f, 0.5f);
    maskRaw = (maskRaw + 1.0f) * 0.5f; // [0, 1]
    float mask = smoothstepEdge(0.35f, 0.65f, maskRaw);

    const float hillFreq = 1.0f / 90.0f;
    float hills = fbm(worldX * hillFreq, worldZ * hillFreq, 4, 2.0f, 0.5f); // [-1, 1]

    return hills * kMaxHillHeight * mask;
}

}

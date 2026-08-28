#include "ShaderLibrary.h"

namespace Harmonia {

// ─────────────────────────────────────────────────────────────────────────────
// GLSL Sources — OpenGL 3.3 Core Profile
// ─────────────────────────────────────────────────────────────────────────────

// Instanced voxel cube — each instance has: world pos (vec3), colour (vec4), glow (float)
static const char* kVoxelVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;        // unit cube vertex
layout(location=1) in vec3 aNorm;       // cube face normal
layout(location=2) in vec3 iOffset;     // instance: world position
layout(location=3) in vec4 iColour;     // instance: RGBA colour
layout(location=4) in float iGlow;      // instance: activation 0-1

uniform mat4 uVP;
uniform float uVoxelSize;
uniform vec3 uCamPos;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vColour;
out float vGlow;
out float vFresnel;

void main() {
    vec3 worldPos = iOffset + aPos * uVoxelSize;
    vWorldPos = worldPos;
    vNormal   = aNorm;
    vColour   = iColour;
    vGlow     = iGlow;

    // Fresnel rim for glow effect
    vec3 viewDir = normalize(uCamPos - worldPos);
    vFresnel = 1.0 - max(dot(aNorm, viewDir), 0.0);

    gl_Position = uVP * vec4(worldPos, 1.0);
}
)";

static const char* kVoxelFrag = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vColour;
in float vGlow;
in float vFresnel;

out vec4 fragColour;

uniform vec3 uSunDir;
uniform float uTime;

void main() {
    // Directional light
    float ndl = max(dot(normalize(vNormal), normalize(uSunDir)), 0.1);

    // Ambient + diffuse
    vec3 col = vColour.rgb * (0.3 + 0.7 * ndl);

    // Glow rim — activation brightness + pulsing
    float pulse = 0.5 + 0.5 * sin(uTime * 6.28 * 2.0 + vWorldPos.x * 0.5);
    float glow  = vGlow * (0.6 + 0.4 * pulse);
    col += vColour.rgb * glow * vFresnel * 2.0;

    // Emission from core when highly active
    col += vColour.rgb * vGlow * 0.4;

    fragColour = vec4(col, vColour.a * (0.4 + 0.6 * max(vGlow, 0.05)));
}
)";

// Particle billboard
static const char* kParticleVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColour;
layout(location=2) in float aSize;
layout(location=3) in float aAlpha;

uniform mat4 uVP;
uniform vec3 uRight;
uniform vec3 uUp;

out vec4 vColour;

void main() {
    vColour = vec4(aColour.rgb, aColour.a * aAlpha);
    gl_Position  = uVP * vec4(aPos, 1.0);
    gl_PointSize = aSize;
}
)";

static const char* kParticleFrag = R"(
#version 330 core
in vec4 vColour;
out vec4 fragColour;

void main() {
    // Soft circle
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float d  = dot(uv, uv);
    if (d > 1.0) discard;
    float a  = 1.0 - smoothstep(0.3, 1.0, d);
    fragColour = vec4(vColour.rgb, vColour.a * a);
}
)";

// Star field — points with brightness
static const char* kStarVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aBrightness;

uniform mat4 uVP;

out float vBright;

void main() {
    vBright = aBrightness;
    gl_Position  = uVP * vec4(aPos, 1.0);
    gl_PointSize = 1.5 + aBrightness * 2.0;
}
)";

static const char* kStarFrag = R"(
#version 330 core
in float vBright;
out vec4 fragColour;

void main() {
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float a  = 1.0 - smoothstep(0.0, 1.0, dot(uv,uv));
    fragColour = vec4(vec3(0.8 + 0.2*vBright), a * vBright);
}
)";

// Simple fullscreen quad shaders for post-process (bloom)
static const char* kQuadVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)";

static const char* kBloomFrag = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColour;
uniform sampler2D uScene;
uniform vec2 uTexelSize;
uniform float uStrength;

void main() {
    // 9-tap box blur (cheap bloom)
    vec3 col = vec3(0.0);
    float w = uStrength;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            col += texture(uScene, vUV + vec2(x,y) * uTexelSize * 3.0).rgb;
    col /= 9.0;
    fragColour = vec4(mix(texture(uScene, vUV).rgb, col, 0.4), 1.0);
}
)";

// Ground plane grid
static const char* kGroundVert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vPos;
void main() { vPos = aPos; gl_Position = uVP * vec4(aPos,1.0); }
)";

static const char* kGroundFrag = R"(
#version 330 core
in vec3 vPos;
out vec4 fragColour;

void main() {
    // Infinite grid
    vec2 grid = abs(fract(vPos.xz - 0.5) - 0.5) / fwidth(vPos.xz);
    float line = min(grid.x, grid.y);
    float alpha = 1.0 - min(line, 1.0);
    alpha *= 0.3 * (1.0 - smoothstep(0.0, 60.0, length(vPos.xz)));
    fragColour = vec4(0.2, 0.3, 0.5, alpha);
}
)";

// ─────────────────────────────────────────────────────────────────────────────

static std::unique_ptr<juce::OpenGLShaderProgram> makeShader(
    juce::OpenGLContext& ctx,
    const char* vert, const char* frag,
    const char* label)
{
    auto prog = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    if (prog->addVertexShader(vert) && prog->addFragmentShader(frag) && prog->link())
        return prog;
    DBG("Shader link failed [" << label << "]: " << prog->getLastError());
    return nullptr;
}

void ShaderLibrary::initialise(juce::OpenGLContext& ctx) {
    voxelShader_     = makeShader(ctx, kVoxelVert,    kVoxelFrag,    "voxel");
    particleShader_  = makeShader(ctx, kParticleVert, kParticleFrag, "particle");
    starfieldShader_ = makeShader(ctx, kStarVert,     kStarFrag,     "starfield");
    bloomShader_     = makeShader(ctx, kQuadVert,      kBloomFrag,    "bloom");
    groundShader_    = makeShader(ctx, kGroundVert,   kGroundFrag,   "ground");
    // sweep + avatar shaders reuse voxel/particle for now
    sweepShader_  = makeShader(ctx, kVoxelVert, kVoxelFrag, "sweep");
    avatarShader_ = makeShader(ctx, kParticleVert, kParticleFrag, "avatar");
}

void ShaderLibrary::shutdown() {
    voxelShader_.reset();
    particleShader_.reset();
    starfieldShader_.reset();
    bloomShader_.reset();
    groundShader_.reset();
    sweepShader_.reset();
    avatarShader_.reset();
}

} // namespace Harmonia

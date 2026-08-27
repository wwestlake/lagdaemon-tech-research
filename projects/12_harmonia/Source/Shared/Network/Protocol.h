#pragma once
//==============================================================================
// Harmonia — HARP Protocol Definition
// HARP = Harmonia Application-layer Real-time Protocol
//
// Port:    4440  (A440 — the concert pitch tuning standard)
// Transport: TCP/IP (JUCE StreamingSocket)
// Byte order: little-endian throughout
//
// Packet framing (TCP is a stream — explicit framing required):
//
//   HANDSHAKE packet (first packet from client only):
//   ┌──────────┬─────────┬──────────┬───────────┬─────────┐
//   │ 4 bytes  │ 1 byte  │ 2 bytes  │  4 bytes  │ N bytes │
//   │  "HARP"  │ version │ MsgType  │ payloadLen│ payload │
//   └──────────┴─────────┴──────────┴───────────┴─────────┘
//
//   All subsequent packets:
//   ┌─────────┬──────────┬───────────┬─────────┐
//   │ 1 byte  │ 2 bytes  │  4 bytes  │ N bytes │
//   │ version │ MsgType  │ payloadLen│ payload │
//   └─────────┴──────────┴───────────┴─────────┘
//
//   Header size (normal): 7 bytes
//   Handshake header:    11 bytes
//==============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace Harmonia { namespace Net {

//── Constants ─────────────────────────────────────────────────────────────────

inline constexpr int      kHarmoniaPort     = 4440;
inline constexpr uint8_t  kProtocolVersion  = 1;
inline constexpr uint32_t kHarpMagic        = 0x50524148; // "HARP" little-endian
inline constexpr uint32_t kMaxPayloadBytes  = 1024 * 1024; // 1MB safety cap
inline constexpr int      kTickRateHz       = 20;
inline constexpr int      kPingIntervalMs   = 5000;

//── Message Types ─────────────────────────────────────────────────────────────

enum class MsgType : uint16_t
{
    // ── Handshake / Session ─────────────────────────────────
    Hello            = 0x0001,  // C→S  playerName(str), clientVersion(u16)
    Welcome          = 0x0002,  // S→C  playerID(u32), serverName(str), sessionName(str), tickRate(u8)
    Goodbye          = 0x0003,  // C↔S  reason(str) — graceful disconnect
    Ping             = 0x0004,  // C↔S  timestamp(u64 ms since epoch)
    Pong             = 0x0005,  // C↔S  echo timestamp(u64)
    ServerInfo       = 0x0006,  // S→C  serverName(str), playerCount(u8), maxPlayers(u8), tickRate(u8)
    Error            = 0x0007,  // S→C  errorCode(u16), message(str)

    // ── Player Presence ─────────────────────────────────────
    PlayerJoined     = 0x0010,  // S→All  playerID(u32), name(str), colorHue(f32), x(f32),y(f32),z(f32)
    PlayerLeft       = 0x0011,  // S→All  playerID(u32), reason(str)
    PlayerPosition   = 0x0012,  // C→S & S→All  playerID(u32), x(f32),y(f32),z(f32), yaw(f32)
    PlayerChat       = 0x0013,  // C→S & S→All  playerID(u32), message(str)

    // ── Notes / Audio Events ────────────────────────────────
    NoteOn           = 0x0020,  // C→S & S→All  playerID(u32), midiNote(u8), velocity(u8), channel(u8)
    NoteOff          = 0x0021,  // C→S & S→All  playerID(u32), midiNote(u8), channel(u8)

    // ── Living Grid (World 1 — 3D Cellular Automaton) ───────
    VoxelSeedRequest = 0x0030,  // C→S  x(u8),y(u8),z(u8), state(f32) — request to seed a voxel
    VoxelDelta       = 0x0031,  // S→All  count(u16), [x(u8),y(u8),z(u8),state(f32)]×count
    VoxelFullSync    = 0x0032,  // S→C  gridW(u8),gridH(u8),gridD(u8), [state(f32)]×W×H×D
    CAParamChange    = 0x0033,  // C→S  ruleType(u8), paramID(u8), value(f32)
    CAAdvance        = 0x0034,  // S→All  generationNumber(u32) — server stepped the CA

    // ── Chord Architect (World 3) ────────────────────────────
    ChordStackChange = 0x0040,  // S→All  count(u8), [midiNote(u8)]×count
    ChordNoteAdd     = 0x0041,  // C→S  playerID(u32), midiNote(u8)
    ChordNoteRemove  = 0x0042,  // C→S  playerID(u32), midiNote(u8)
    ChordClear       = 0x0043,  // C→S  playerID(u32) — clear the shared stack

    // ── Interval Forge (World 2) ─────────────────────────────
    IntervalChange   = 0x0050,  // C→S & S→All  playerID(u32), rootNote(u8), intervalSemitones(i8)

    // ── World / Session Management ───────────────────────────
    WorldStateSync   = 0x0060,  // S→C  full world snapshot on join (composite)
};

//── Wire Structures ───────────────────────────────────────────────────────────
// These are NOT directly memcpy'd to the wire — use HarpSerializer to
// read/write. Defined here for documentation and type safety.

#pragma pack(push, 1)

struct HandshakeHeader {
    uint32_t magic;        // kHarpMagic = 0x50524148
    uint8_t  version;      // kProtocolVersion
    uint16_t msgType;      // MsgType as uint16
    uint32_t payloadLen;   // bytes following this header
};

struct PacketHeader {
    uint8_t  version;      // kProtocolVersion
    uint16_t msgType;      // MsgType as uint16
    uint32_t payloadLen;   // bytes following this header
};

#pragma pack(pop)

inline constexpr int kHandshakeHeaderSize = sizeof(HandshakeHeader); // 11
inline constexpr int kPacketHeaderSize    = sizeof(PacketHeader);    //  7

//── CA Rule Types (for CAParamChange.ruleType) ────────────────────────────────
enum class CARuleType : uint8_t
{
    GameOfLife3D    = 0,
    GrayScott       = 1,
    Lenia           = 2,
};

//── CA Parameter IDs (for CAParamChange.paramID) ─────────────────────────────
enum class CAParamID : uint8_t
{
    // Game of Life 3D
    GoL_SurvivalMin = 0,
    GoL_SurvivalMax = 1,
    GoL_BirthMin    = 2,
    GoL_BirthMax    = 3,
    GoL_MutationRate= 4,

    // Gray-Scott
    GS_FeedRate     = 10,
    GS_KillRate     = 11,
    GS_DiffU        = 12,
    GS_DiffV        = 13,

    // Lenia
    Lenia_R         = 20,
    Lenia_Mu        = 21,
    Lenia_Sigma     = 22,
    Lenia_DeltaT    = 23,
};

//── Error Codes ───────────────────────────────────────────────────────────────
enum class ErrorCode : uint16_t
{
    None            = 0,
    VersionMismatch = 1,
    SessionFull     = 2,
    InvalidPacket   = 3,
    SessionNotFound = 4,
    NotAuthorized   = 5,
    InternalError   = 6,
};

} // namespace Net
} // namespace Harmonia


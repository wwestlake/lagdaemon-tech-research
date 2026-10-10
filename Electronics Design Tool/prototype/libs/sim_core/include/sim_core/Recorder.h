#pragma once

// Recording (architecture section 11, decision F8).
//
// A run is two files: a compact columnar binary data file (*.simrec) and a
// JSON manifest (*.manifest.json).
//
// Data file, format 1.0, little-endian:
//   header  "WBSIMREC" | u16 major | u16 minor | u32 reserved | i64 ticks per second
//   chunks  u32 type | u32 payload bytes | payload | u32 CRC-32 of (type, size, payload)
//     1 samples:     u32 signal | u32 value type | u32 n | i64 tick[n] | u32 microstep[n] | 8-byte value[n]
//     2 events:      u32 n | n x (i64 tick, u32 microstep, i32 priority, i32 source, i32 signal, u32 flags, i64 code, f64 value)
//     3 diagnostics: u32 n | n x (i64 tick, u32 microstep, u8 severity, u8 category, u16 0, u32 len, participant, u32 len, message)
//     4 end:         u64 rounds
// Timestamps are integer ticks, never floats; per signal and for events they
// never decrease (checked on write and on read). Readers accept older minor
// versions and skip unknown chunk types of the same major; a newer major is
// refused. The data file holds only deterministic content, so a replayed run
// reproduces it byte for byte; wall-clock facts live in the manifest.
//
// The writer runs its own thread (decision F5): the scheduler hands each
// round over a queue and never waits for the disk.

#include "sim_core/Json.h"
#include "sim_core/Simulation.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sim
{
inline constexpr std::uint16_t kRecordingMajor = 1;
inline constexpr std::uint16_t kRecordingMinor = 0;
inline constexpr int kManifestSchemaVersion = 1;

class RecordingWriter final : public RecordingSink
{
public:
    RecordingWriter(std::string dataPath, std::string manifestPath);
    ~RecordingWriter() override;

    void begin(const RunDescription& run) override;
    void round(RoundRecord&& record) override;
    void end() override;

    // Waits for the writer thread to finish (end() was called). Write errors,
    // if any, are reported here.
    Status finish();
    // Samples per signal (and events/diagnostics) per chunk. Default 4096.
    void setChunkSize(int samples) { chunkSize = samples < 1 ? 1 : samples; }

private:
    struct Column
    {
        ValueType type = ValueType::Real;
        std::vector<Tick> ticks;
        std::vector<std::uint32_t> microsteps;
        std::vector<std::uint64_t> values;
        SimTime last { -1, 0 };
        bool any = false;
    };
    void run();
    void consume(RoundRecord& record);
    void writeChunk(std::uint32_t type, const std::vector<std::uint8_t>& payload);
    void flushColumn(int signal, Column& column);
    void flushEvents();
    void flushDiagnostics();
    void error(const std::string& message);

    std::string dataPath, manifestPath;
    RunDescription description;
    int chunkSize = 4096;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<RoundRecord> queue;
    bool started = false, ending = false, finished = false;

    std::FILE* file = nullptr;
    std::map<int, Column> columns;
    std::vector<Event> events;
    std::vector<Diagnostic> diagnosticsBuffer;
    SimTime lastEvent { -1, 0 };
    std::uint64_t rounds = 0;
    std::string firstError;
};

struct RecordedSample
{
    Tick tick = 0;
    std::uint32_t microstep = 0;
    Value value;
};

struct Recording
{
    std::uint16_t major = 0, minor = 0;
    Tick ticksPerSecond = 0;
    std::map<int, std::vector<RecordedSample>> samples; // by signal id
    std::vector<Event> events;
    std::vector<Diagnostic> diagnostics;
    std::uint64_t rounds = 0;
    bool complete = false; // the end chunk was present

    // External inputs (tests, UI, agent, hardware) in recorded order: what
    // replay re-applies.
    std::vector<Event> externalInputs() const;
};

Status readRecording(const std::string& path, Recording& out);
Status loadManifest(const std::string& path, Json& out);
Status saveManifest(const std::string& path, const Json& manifest);
// One row per sample: tick, microstep, seconds, signal, unit, value. The
// header states the tick resolution and each signal's unit.
Status exportCsv(const Recording& recording, const Json& manifest, const std::string& path);
// The chunk checksum (CRC-32, IEEE polynomial), continuing from `crc`.
std::uint32_t recordingCrc32(const void* data, std::size_t size, std::uint32_t crc = 0);
// Byte-for-byte comparison of two files (replay verification).
bool filesIdentical(const std::string& a, const std::string& b, std::string& difference);
}

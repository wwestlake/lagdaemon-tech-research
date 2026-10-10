#include "sim_core/Recorder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifndef SIM_CORE_BUILD_OPTIMIZED
#define SIM_CORE_BUILD_OPTIMIZED 0
#endif

namespace sim
{
namespace
{
const char kMagic[8] = { 'W', 'B', 'S', 'I', 'M', 'R', 'E', 'C' };
enum ChunkType : std::uint32_t { kSamples = 1, kEvents = 2, kDiagnostics = 3, kEnd = 4 };

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t {};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

template <typename T>
void put(std::vector<std::uint8_t>& out, T v)
{
    const auto at = out.size();
    out.resize(at + sizeof(T));
    std::memcpy(out.data() + at, &v, sizeof(T));
}

void putString(std::vector<std::uint8_t>& out, const std::string& s)
{
    put<std::uint32_t>(out, (std::uint32_t)s.size());
    out.insert(out.end(), s.begin(), s.end());
}

struct Cursor
{
    const std::uint8_t* p;
    std::size_t n, i = 0;
    template <typename T>
    bool get(T& v)
    {
        if (i + sizeof(T) > n) return false;
        std::memcpy(&v, p + i, sizeof(T));
        i += sizeof(T);
        return true;
    }
    bool getString(std::string& s)
    {
        std::uint32_t len = 0;
        if (!get(len) || i + len > n) return false;
        s.assign((const char*)p + i, len);
        i += len;
        return true;
    }
};

std::uint64_t bitsOf(const Value& v)
{
    std::uint64_t bits = 0;
    if (v.type == ValueType::Real) std::memcpy(&bits, &v.real, 8);
    else std::memcpy(&bits, &v.integer, 8);
    return bits;
}

Value valueOf(ValueType type, std::uint64_t bits)
{
    Value v;
    v.type = type;
    if (type == ValueType::Real) std::memcpy(&v.real, &bits, 8);
    else std::memcpy(&v.integer, &bits, 8);
    return v;
}

const char* timingName(TimingKind k)
{
    switch (k) { case TimingKind::FixedStep: return "fixed"; case TimingKind::VariableStep: return "variable"; case TimingKind::Sampled: return "sampled"; }
    return "?";
}
}

std::uint32_t recordingCrc32(const void* data, std::size_t size, std::uint32_t crc)
{
    return crc32((const std::uint8_t*)data, size, crc);
}

// ---- writer -----------------------------------------------------------------

RecordingWriter::RecordingWriter(std::string data, std::string manifest)
    : dataPath(std::move(data)), manifestPath(std::move(manifest))
{
}

RecordingWriter::~RecordingWriter()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        ending = true;
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    if (file != nullptr)
        std::fclose(file);
}

void RecordingWriter::error(const std::string& message)
{
    if (firstError.empty())
        firstError = message;
}

void RecordingWriter::begin(const RunDescription& runDescription)
{
    description = runDescription;
    for (const auto& s : runDescription.signals)
        if (s.spec.kind != SignalKind::Event)
            columns[s.id].type = s.spec.type;
    file = std::fopen(dataPath.c_str(), "wb");
    if (file == nullptr)
        error("Could not create " + dataPath + ".");
    else
    {
        std::vector<std::uint8_t> header(kMagic, kMagic + 8);
        put<std::uint16_t>(header, kRecordingMajor);
        put<std::uint16_t>(header, kRecordingMinor);
        put<std::uint32_t>(header, 0);
        put<std::int64_t>(header, kTicksPerSecond);
        std::fwrite(header.data(), 1, header.size(), file);
    }
    started = true;
    worker = std::thread([this] { run(); });
}

void RecordingWriter::round(RoundRecord&& record)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(record));
    }
    wake.notify_one();
}

void RecordingWriter::end()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        ending = true;
    }
    wake.notify_all();
}

Status RecordingWriter::finish()
{
    end();
    if (worker.joinable())
        worker.join();
    return firstError.empty() ? Status {} : Status::failure(firstError);
}

void RecordingWriter::run()
{
    for (;;)
    {
        RoundRecord record;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return !queue.empty() || ending; });
            if (queue.empty())
                break;
            record = std::move(queue.front());
            queue.pop_front();
        }
        consume(record);
    }

    // End of the run: everything still buffered, in signal order, then the end chunk.
    for (auto& [signal, column] : columns)
        flushColumn(signal, column);
    flushEvents();
    flushDiagnostics();
    std::vector<std::uint8_t> payload;
    put<std::uint64_t>(payload, rounds);
    writeChunk(kEnd, payload);
    if (file != nullptr)
    {
        if (std::fclose(file) != 0)
            error("Could not finish writing " + dataPath + ".");
        file = nullptr;
    }

    Json m = Json::object();
    m.set("schemaVersion", Json::integer(kManifestSchemaVersion));
    m.set("format", Json::string("wb-sim-recording"));
    m.set("formatVersion", Json::string(std::to_string(kRecordingMajor) + "." + std::to_string(kRecordingMinor)));
    Json resolution = Json::object();
    resolution.set("ticksPerSecond", Json::integer(kTicksPerSecond));
    resolution.set("unit", Json::string("ps"));
    m.set("tickResolution", resolution);
    m.set("t0", Json::integer(description.t0));
    m.set("rounds", Json::integer((std::int64_t)rounds));
    Json signals = Json::array();
    for (const auto& s : description.signals)
    {
        Json j = Json::object();
        j.set("id", Json::integer(s.id));
        j.set("name", Json::string(s.name));
        j.set("kind", Json::string(toString(s.spec.kind)));
        j.set("type", Json::string(toString(s.spec.type)));
        j.set("unit", Json::string(s.spec.kind == SignalKind::Event ? std::string() : s.unit.symbol));
        j.set("role", Json::string(toString(s.spec.role)));
        j.set("writer", Json::string(s.writer < 0 ? std::string("external") : description.participants[(std::size_t)s.writer].name));
        signals.push(j);
    }
    m.set("signals", signals);
    Json participants = Json::array();
    for (const auto& p : description.participants)
    {
        Json j = Json::object();
        j.set("name", Json::string(p.name));
        j.set("order", Json::integer(p.order));
        j.set("timing", Json::string(timingName(p.timing.kind)));
        j.set("stepTicks", Json::integer(p.timing.step));
        Json caps = Json::object();
        caps.set("canRollback", Json::boolean(p.caps.canRollback));
        caps.set("locatesEvents", Json::boolean(p.caps.locatesEvents));
        caps.set("providesNextEventTime", Json::boolean(p.caps.providesNextEventTime));
        caps.set("stateSerializable", Json::boolean(p.caps.stateSerializable));
        caps.set("deterministic", Json::boolean(p.caps.deterministic));
        j.set("capabilities", caps);
        j.set("seed", Json::integer((std::int64_t)p.seed));
        participants.push(j);
    }
    m.set("participants", participants);
    Json build = Json::object();
    build.set("library", Json::string("sim_core"));
    build.set("configuration", Json::string(SIM_CORE_BUILD_OPTIMIZED ? "optimized" : "unoptimized"));
    build.set("compiled", Json::string(std::string(__DATE__) + " " + __TIME__));
    m.set("build", build);
    if (auto s = saveManifest(manifestPath, m); !s)
        error(s.message);
    finished = true;
}

void RecordingWriter::consume(RoundRecord& record)
{
    rounds = std::max<std::uint64_t>(rounds, record.round);
    for (const auto& sample : record.samples)
    {
        auto& column = columns[sample.signal];
        if (column.any && sample.time < column.last)
            error("Signal " + std::to_string(sample.signal) + ": a sample at tick " + std::to_string(sample.time.tick) + " came after tick "
                  + std::to_string(column.last.tick) + " (timestamps must not decrease).");
        column.any = true;
        column.last = sample.time;
        column.ticks.push_back(sample.time.tick);
        column.microsteps.push_back(sample.time.microstep);
        column.values.push_back(bitsOf(sample.value));
        if ((int)column.ticks.size() >= chunkSize)
            flushColumn(sample.signal, column);
    }
    for (const auto& e : record.events)
    {
        if (e.time < lastEvent)
            error("An event at tick " + std::to_string(e.time.tick) + " came after tick " + std::to_string(lastEvent.tick) + ".");
        lastEvent = e.time;
        events.push_back(e);
        if ((int)events.size() >= chunkSize)
            flushEvents();
    }
    for (const auto& d : record.diagnostics)
    {
        diagnosticsBuffer.push_back(d);
        if ((int)diagnosticsBuffer.size() >= chunkSize)
            flushDiagnostics();
    }
}

void RecordingWriter::writeChunk(std::uint32_t type, const std::vector<std::uint8_t>& payload)
{
    if (file == nullptr)
        return;
    std::vector<std::uint8_t> head;
    put<std::uint32_t>(head, type);
    put<std::uint32_t>(head, (std::uint32_t)payload.size());
    std::uint32_t crc = crc32(head.data(), head.size());
    crc = crc32(payload.data(), payload.size(), crc);
    std::vector<std::uint8_t> tail;
    put<std::uint32_t>(tail, crc);
    if (std::fwrite(head.data(), 1, head.size(), file) != head.size()
        || (!payload.empty() && std::fwrite(payload.data(), 1, payload.size(), file) != payload.size())
        || std::fwrite(tail.data(), 1, tail.size(), file) != tail.size())
        error("Could not write to " + dataPath + ".");
}

void RecordingWriter::flushColumn(int signal, Column& column)
{
    if (column.ticks.empty())
        return;
    std::vector<std::uint8_t> payload;
    put<std::uint32_t>(payload, (std::uint32_t)signal);
    put<std::uint32_t>(payload, (std::uint32_t)column.type);
    put<std::uint32_t>(payload, (std::uint32_t)column.ticks.size());
    for (auto t : column.ticks) put<std::int64_t>(payload, t);
    for (auto m : column.microsteps) put<std::uint32_t>(payload, m);
    for (auto v : column.values) put<std::uint64_t>(payload, v);
    writeChunk(kSamples, payload);
    column.ticks.clear();
    column.microsteps.clear();
    column.values.clear();
}

void RecordingWriter::flushEvents()
{
    if (events.empty())
        return;
    std::vector<std::uint8_t> payload;
    put<std::uint32_t>(payload, (std::uint32_t)events.size());
    for (const auto& e : events)
    {
        put<std::int64_t>(payload, e.time.tick);
        put<std::uint32_t>(payload, e.time.microstep);
        put<std::int32_t>(payload, e.priority);
        put<std::int32_t>(payload, e.source);
        put<std::int32_t>(payload, e.signal);
        put<std::uint32_t>(payload, e.flags);
        put<std::int64_t>(payload, e.payload.code);
        put<double>(payload, e.payload.value);
    }
    writeChunk(kEvents, payload);
    events.clear();
}

void RecordingWriter::flushDiagnostics()
{
    if (diagnosticsBuffer.empty())
        return;
    std::vector<std::uint8_t> payload;
    put<std::uint32_t>(payload, (std::uint32_t)diagnosticsBuffer.size());
    for (const auto& d : diagnosticsBuffer)
    {
        put<std::int64_t>(payload, d.time.tick);
        put<std::uint32_t>(payload, d.time.microstep);
        put<std::uint8_t>(payload, (std::uint8_t)d.severity);
        put<std::uint8_t>(payload, (std::uint8_t)d.category);
        put<std::uint16_t>(payload, 0);
        putString(payload, d.participant);
        putString(payload, d.message);
    }
    writeChunk(kDiagnostics, payload);
    diagnosticsBuffer.clear();
}

// ---- reader -----------------------------------------------------------------

std::vector<Event> Recording::externalInputs() const
{
    std::vector<Event> list;
    for (const auto& e : events)
        if (e.flags & Event::External)
            list.push_back(e);
    return list;
}

Status readRecording(const std::string& path, Recording& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Status::failure("Could not open " + path + ".");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Recording r;
    if (bytes.size() < 24 || std::memcmp(bytes.data(), kMagic, 8) != 0)
        return Status::failure(path + " is not a Workbench simulation recording.");
    Cursor header { bytes.data() + 8, 16 };
    std::uint32_t reserved = 0;
    header.get(r.major);
    header.get(r.minor);
    header.get(reserved);
    header.get(r.ticksPerSecond);
    if (r.major > kRecordingMajor)
        return Status::failure(path + " is recording format " + std::to_string(r.major) + "." + std::to_string(r.minor)
                               + ", newer than this reader (" + std::to_string(kRecordingMajor) + "." + std::to_string(kRecordingMinor)
                               + "). Update the Workbench to read it.");
    if (r.major < 1 || r.ticksPerSecond <= 0)
        return Status::failure(path + ": the header is invalid.");

    std::map<int, SimTime> last;
    SimTime lastEvent { -1, 0 };
    std::size_t offset = 24;
    int chunk = 0;
    while (offset < bytes.size())
    {
        if (offset + 8 > bytes.size())
            return Status::failure(path + ": chunk " + std::to_string(chunk) + " at byte " + std::to_string(offset) + " is truncated.");
        std::uint32_t type = 0, size = 0;
        std::memcpy(&type, bytes.data() + offset, 4);
        std::memcpy(&size, bytes.data() + offset + 4, 4);
        if (offset + 8 + (std::size_t)size + 4 > bytes.size())
            return Status::failure(path + ": chunk " + std::to_string(chunk) + " (type " + std::to_string(type) + ") at byte " + std::to_string(offset) + " is truncated.");
        std::uint32_t stored = 0;
        std::memcpy(&stored, bytes.data() + offset + 8 + size, 4);
        if (crc32(bytes.data() + offset, 8 + (std::size_t)size) != stored)
            return Status::failure(path + ": chunk " + std::to_string(chunk) + " (type " + std::to_string(type) + ") at byte " + std::to_string(offset)
                                   + " is corrupted (checksum mismatch).");
        Cursor c { bytes.data() + offset + 8, size };
        auto bad = [&](const std::string& what) {
            return Status::failure(path + ": chunk " + std::to_string(chunk) + " at byte " + std::to_string(offset) + ": " + what);
        };
        if (type == kSamples)
        {
            std::uint32_t signal = 0, vtype = 0, n = 0;
            if (!c.get(signal) || !c.get(vtype) || !c.get(n) || vtype > 2 || c.i + (std::size_t)n * 20 != c.n)
                return bad("malformed samples.");
            std::vector<Tick> ticks(n);
            std::vector<std::uint32_t> micro(n);
            std::vector<std::uint64_t> values(n);
            for (auto& t : ticks) c.get(t);
            for (auto& m : micro) c.get(m);
            for (auto& v : values) c.get(v);
            auto& column = r.samples[(int)signal];
            for (std::uint32_t k = 0; k < n; ++k)
            {
                const SimTime t { ticks[k], micro[k] };
                auto found = last.find((int)signal);
                if (found != last.end() && t < found->second)
                    return bad("signal " + std::to_string(signal) + " goes back in time (tick " + std::to_string(t.tick) + " after " + std::to_string(found->second.tick) + ").");
                last[(int)signal] = t;
                column.push_back({ ticks[k], micro[k], valueOf((ValueType)vtype, values[k]) });
            }
        }
        else if (type == kEvents)
        {
            std::uint32_t n = 0;
            if (!c.get(n) || c.i + (std::size_t)n * 44 != c.n)
                return bad("malformed events.");
            for (std::uint32_t k = 0; k < n; ++k)
            {
                Event e;
                c.get(e.time.tick);
                c.get(e.time.microstep);
                c.get(e.priority);
                c.get(e.source);
                c.get(e.signal);
                c.get(e.flags);
                c.get(e.payload.code);
                c.get(e.payload.value);
                if (e.time < lastEvent)
                    return bad("events go back in time (tick " + std::to_string(e.time.tick) + ").");
                lastEvent = e.time;
                r.events.push_back(e);
            }
        }
        else if (type == kDiagnostics)
        {
            std::uint32_t n = 0;
            if (!c.get(n))
                return bad("malformed diagnostics.");
            for (std::uint32_t k = 0; k < n; ++k)
            {
                Diagnostic d;
                std::uint8_t severity = 0, category = 0;
                std::uint16_t pad = 0;
                if (!c.get(d.time.tick) || !c.get(d.time.microstep) || !c.get(severity) || !c.get(category) || !c.get(pad)
                    || !c.getString(d.participant) || !c.getString(d.message))
                    return bad("malformed diagnostics.");
                d.severity = (Severity)severity;
                d.category = (Category)category;
                r.diagnostics.push_back(d);
            }
        }
        else if (type == kEnd)
        {
            if (!c.get(r.rounds))
                return bad("malformed end.");
            r.complete = true;
        }
        // Unknown chunk types of this major version are skipped.
        offset += 8 + (std::size_t)size + 4;
        ++chunk;
    }
    out = std::move(r);
    return {};
}

Status loadManifest(const std::string& path, Json& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Status::failure("Could not open " + path + ".");
    std::stringstream text;
    text << in.rdbuf();
    std::string error;
    Json m;
    if (!Json::parse(text.str(), m, error))
        return Status::failure(path + ": " + error);
    const Json* schema = m.find("schemaVersion");
    if (schema == nullptr || schema->asInteger(0) < 1)
        return Status::failure(path + " has no schemaVersion; it is not a simulation manifest.");
    if (schema->asInteger(0) > kManifestSchemaVersion)
        return Status::failure(path + " uses manifest schema " + std::to_string(schema->asInteger(0)) + ", newer than this reader ("
                               + std::to_string(kManifestSchemaVersion) + ").");
    out = std::move(m);
    return {};
}

Status saveManifest(const std::string& path, const Json& manifest)
{
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o)
        return Status::failure("Could not create " + path + ".");
    o << manifest.dump(2) << "\n";
    return o.good() ? Status {} : Status::failure("Could not write " + path + ".");
}

Status exportCsv(const Recording& recording, const Json& manifest, const std::string& path)
{
    std::map<int, std::pair<std::string, std::string>> names; // id -> name, unit
    if (const Json* signals = manifest.find("signals"))
        for (const auto& s : signals->items())
        {
            const Json* id = s.find("id");
            const Json* name = s.find("name");
            const Json* unit = s.find("unit");
            if (id != nullptr)
                names[(int)id->asInteger()] = { name ? name->asString() : std::string(), unit ? unit->asString() : std::string() };
        }
    struct Row { Tick tick; std::uint32_t micro; int signal; Value value; };
    std::vector<Row> rows;
    for (const auto& [signal, samples] : recording.samples)
        for (const auto& s : samples)
            rows.push_back({ s.tick, s.microstep, signal, s.value });
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.micro != b.micro) return a.micro < b.micro;
        return a.signal < b.signal;
    });
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o)
        return Status::failure("Could not create " + path + ".");
    o << "# Workbench System Simulator recording, format " << recording.major << "." << recording.minor << "\n";
    o << "# tick resolution: " << recording.ticksPerSecond << " ticks per second (1 tick = 1 ps)\n";
    for (const auto& [id, nu] : names)
        o << "# signal " << id << ": " << nu.first << " [" << (nu.second.empty() ? "-" : nu.second) << "]\n";
    o << "tick,microstep,seconds,signal,unit,value\n";
    char buffer[64];
    for (const auto& r : rows)
    {
        const auto& nu = names[r.signal];
        std::snprintf(buffer, sizeof buffer, "%.12f", ticksToSeconds(r.tick));
        o << r.tick << "," << r.micro << "," << buffer << "," << nu.first << "," << nu.second << ",";
        if (r.value.type == ValueType::Real)
        {
            std::snprintf(buffer, sizeof buffer, "%.17g", r.value.real);
            o << buffer;
        }
        else
            o << r.value.integer;
        o << "\n";
    }
    return o.good() ? Status {} : Status::failure("Could not write " + path + ".");
}

bool filesIdentical(const std::string& a, const std::string& b, std::string& difference)
{
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb)
    {
        difference = "could not open both files";
        return false;
    }
    std::vector<char> x((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
    std::vector<char> y((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
    if (x.size() != y.size())
    {
        difference = "sizes differ (" + std::to_string(x.size()) + " vs " + std::to_string(y.size()) + " bytes)";
        return false;
    }
    for (std::size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i])
        {
            difference = "first difference at byte " + std::to_string(i);
            return false;
        }
    return true;
}
}

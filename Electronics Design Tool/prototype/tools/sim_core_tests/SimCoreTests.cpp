// P0 acceptance tests for sim_core (architecture revision 3, section 16).
//
// Built twice: sim_core_tests (optimised library) and sim_core_tests_unopt
// (unoptimised library). Each run writes its event-order log, lifecycle log
// and recordings to an output folder; `--compare A B` then checks that the
// two configurations agree (event order and lifecycle exactly, values within
// tolerance).
//
// Usage: sim_core_tests [--out <dir>]
//        sim_core_tests --compare <dirA> <dirB>

#include "sim_core/Json.h"
#include "sim_core/Participant.h"
#include "sim_core/Random.h"
#include "sim_core/Recorder.h"
#include "sim_core/Runner.h"
#include "sim_core/Simulation.h"
#include "sim_core/Units.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef SIM_CORE_TESTS_CONFIG
#define SIM_CORE_TESTS_CONFIG "unknown"
#endif

using namespace sim;
namespace fs = std::filesystem;

namespace
{
int failures = 0, passes = 0;
std::string outDir = "sim_core_test_out";
std::vector<std::string> eventLog, lifecycleLog;

void check(bool ok, const std::string& name, const std::string& detail = {})
{
    if (ok) ++passes; else ++failures;
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() || ok ? "" : "  -- ", ok ? "" : detail.c_str());
}

std::string path(const std::string& file) { return (fs::path(outDir) / file).string(); }
Tick ms(double v) { Tick t = 0; secondsToTicks(v * 1e-3, t); return t; }
Tick sec(double v) { Tick t = 0; secondsToTicks(v, t); return t; }

std::vector<std::uint8_t> bytesOf(double v)
{
    std::vector<std::uint8_t> b(sizeof v);
    std::memcpy(b.data(), &v, sizeof v);
    return b;
}
double doubleFrom(const std::vector<std::uint8_t>& b, std::size_t at = 0)
{
    double v = 0.0;
    std::memcpy(&v, b.data() + at, sizeof v);
    return v;
}

// ---- toy participants with analytic behaviour ---------------------------------

// x(t) = x0 * exp(-a (t - t0)), exact; supplies x, x', x'' so readers can
// extrapolate between its 1 ms steps.
struct ExpDecay final : Participant
{
    std::string name;
    double a = 1.0, x0 = 1.0;
    Tick step = ms(1), t0 = 0;
    double x = 0.0, trial = 0.0;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::continuous("x", "1", 2) };
        i.timing = { TimingKind::FixedStep, step };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    double at(Tick t) const { return x0 * std::exp(-a * ticksToSeconds(t - t0)); }
    Status initialize(Tick start, int, const InputFrame&) override { t0 = start; x = trial = x0; return {}; }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink&) override { trial = at(to); return {}; }
    void commit() override { x = trial; }
    void rollback() override { trial = x; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, trial, -a * trial, a * a * trial); }
    std::vector<std::uint8_t> saveState() const override { return bytesOf(x); }
    Status restoreState(const std::vector<std::uint8_t>& b) override { x = trial = doubleFrom(b); return {}; }
};

// y' = -b y + u(t), RK4, u read from its input with extrapolation.
struct Filter final : Participant
{
    std::string name;
    double b = 3.0, y0 = 0.5;
    Tick step = ms(0.1);
    Tick maxExtrapolation = 0;
    double y = 0.0, trial = 0.0;
    std::shared_ptr<std::vector<std::pair<Tick, double>>> trace;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        auto in = PortSpec::continuous("u", "1");
        in.maxExtrapolation = maxExtrapolation;
        i.inputs = { in };
        i.outputs = { PortSpec::continuous("y", "1") };
        i.timing = { TimingKind::FixedStep, step };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick, int, const InputFrame&) override { y = trial = y0; return {}; }
    StepResult doStep(Tick from, Tick to, const InputFrame& in, EventSink&) override
    {
        const double h = ticksToSeconds(to - from);
        const Tick mid = from + (to - from) / 2;
        auto f = [&](double v, Tick t) { return -b * v + in.real(0, t); };
        const double k1 = f(y, from), k2 = f(y + 0.5 * h * k1, mid), k3 = f(y + 0.5 * h * k2, mid), k4 = f(y + h * k3, to);
        trial = y + h / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
        lastTo = to;
        return {};
    }
    void commit() override
    {
        y = trial;
        if (trace) trace->push_back({ lastTo, y });
    }
    void rollback() override { trial = y; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, trial); }
    std::vector<std::uint8_t> saveState() const override { return bytesOf(y); }
    Status restoreState(const std::vector<std::uint8_t>& bytes) override { y = trial = doubleFrom(bytes); return {}; }
    Tick lastTo = 0;
};

// Emits scripted events exactly at their times (variable step + next event time).
struct Script final : Participant
{
    struct Item { Tick at; int port; std::int64_t code; };
    std::string name;
    std::vector<Item> items;
    std::size_t next = 0, trialNext = 0;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::event("a"), PortSpec::event("b") };
        i.timing = { TimingKind::VariableStep, sec(1), 1 };
        i.caps.canRollback = true;
        i.caps.providesNextEventTime = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick, int, const InputFrame&) override { next = trialNext = 0; return {}; }
    Tick nextEventTime() const override { return next < items.size() ? items[next].at : kNever; }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink& sink) override
    {
        trialNext = next;
        while (trialNext < items.size() && items[trialNext].at <= to)
        {
            sink.emit(items[trialNext].port, to, { items[trialNext].code, 0.0 });
            ++trialNext;
        }
        return {};
    }
    void commit() override { next = trialNext; }
    void rollback() override { trialNext = next; }
    void getOutputs(OutputFrame&) const override {}
    std::vector<std::uint8_t> saveState() const override { std::vector<std::uint8_t> b(sizeof next); std::memcpy(b.data(), &next, sizeof next); return b; }
    Status restoreState(const std::vector<std::uint8_t>& b) override { std::memcpy(&next, b.data(), sizeof next); trialNext = next; return {}; }
};

// Logs every event it receives; some codes cascade (same-tick re-emission).
struct Logger final : Participant
{
    std::string name = "L";
    std::shared_ptr<std::vector<std::string>> log;
    Tick step = sec(1);
    bool variable = false;
    Tick committed = 0, trialTo = 0;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        for (const char* n : { "e0", "e1", "e2", "e3" })
        {
            auto p = PortSpec::event(n);
            p.required = false;
            i.inputs.push_back(p);
        }
        auto probe = PortSpec::continuous("probe", "1");
        probe.required = false;
        probe.policy = InputPolicy::Hold;
        i.inputs.push_back(probe);
        i.outputs = { PortSpec::event("out") };
        i.timing = { variable ? TimingKind::VariableStep : TimingKind::FixedStep, step, 1 };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick t0, int, const InputFrame&) override { committed = trialTo = t0; return {}; }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink&) override { trialTo = to; return {}; }
    void commit() override { committed = trialTo; }
    void rollback() override { trialTo = committed; }
    void getOutputs(OutputFrame&) const override {}
    Status handleEvent(const Event& e, int port, const InputFrame& in, EventSink& sink) override
    {
        char line[200];
        std::snprintf(line, sizeof line, "%s t=%lld m=%u p=%d c=%lld src=%d %s", name.c_str(), (long long)e.time.tick, e.time.microstep, port,
                      (long long)e.payload.code, e.source, committed == e.time.tick ? "at" : "behind");
        std::string text = line;
        const auto c = e.payload.code;
        if (c == 7777)
        {
            char v[64];
            std::snprintf(v, sizeof v, " probe=%.17g", in.real(4, e.time.tick));
            text += v;
        }
        log->push_back(text);
        if (c > 1000 && c < 2000 && c % 100 != 0) sink.emit(0, e.time.tick, { c - 1, 0.0 }); // cascades down to the next multiple of 100
        if (c == 9999) sink.emit(0, e.time.tick, { 9999, 0.0 });
        if (c == 3000) sink.emit(0, e.time.tick + ms(5), { 3001, 0.0 });
        return {};
    }
    std::vector<std::uint8_t> saveState() const override { std::vector<std::uint8_t> b(sizeof committed); std::memcpy(b.data(), &committed, sizeof committed); return b; }
    Status restoreState(const std::vector<std::uint8_t>& b) override { std::memcpy(&committed, b.data(), sizeof committed); trialTo = committed; return {}; }
};

// A ramp x = rate (t - t0) that locates the time it crosses `threshold`.
struct Locator final : Participant
{
    std::string name;
    Tick crossing = 0;     // the exact crossing tick
    Tick maxStep = ms(10);
    Tick t0 = 0, committed = 0, trialTo = 0;
    int rollbacks = 0;
    std::shared_ptr<std::vector<std::string>> order;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::continuous("x", "1", 1), PortSpec::event("crossed") };
        i.timing = { TimingKind::VariableStep, maxStep, 1 };
        i.caps.canRollback = true;
        i.caps.locatesEvents = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick start, int, const InputFrame&) override { t0 = committed = trialTo = start; return {}; }
    StepResult doStep(Tick from, Tick to, const InputFrame&, EventSink& sink) override
    {
        if (order) order->push_back(name + "@" + std::to_string(to));
        StepResult r;
        trialTo = to;
        if (from < crossing && crossing <= to)
        {
            sink.emit(1, crossing, { 1, 0.0 });
            if (crossing < to)
            {
                r.stoppedEarly = true;
                r.stoppedAt = crossing;
                trialTo = crossing;
            }
        }
        return r;
    }
    void commit() override { committed = trialTo; }
    void rollback() override { ++rollbacks; trialTo = committed; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, ticksToSeconds(trialTo - t0), 1.0); }
    std::vector<std::uint8_t> saveState() const override { std::vector<std::uint8_t> b(sizeof committed); std::memcpy(b.data(), &committed, sizeof committed); return b; }
    Status restoreState(const std::vector<std::uint8_t>& b) override { std::memcpy(&committed, b.data(), sizeof committed); trialTo = committed; return {}; }
};

// A participant that cannot roll back; it records its steps and complains if rolled back.
struct NoRollback final : Participant
{
    std::string name = "NR";
    Tick step = ms(10);
    int rollbacks = 0;
    std::shared_ptr<std::vector<std::string>> order;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::continuous("v", "1") };
        i.timing = { TimingKind::FixedStep, step };
        return i;
    }
    Status initialize(Tick, int, const InputFrame&) override { return {}; }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink&) override { if (order) order->push_back(name + "@" + std::to_string(to)); return {}; }
    void rollback() override { ++rollbacks; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, 0.0); }
};

// Reads a (possibly converted) input and records what it saw at each step.
struct Probe final : Participant
{
    std::string name = "P";
    PortSpec input = PortSpec::continuous("in", "1");
    Tick step = ms(1);
    std::shared_ptr<std::vector<std::pair<Tick, double>>> seen;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.inputs = { input };
        i.timing = { TimingKind::FixedStep, step };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick, int, const InputFrame& in) override { first = in.value(0).asReal(); return {}; }
    StepResult doStep(Tick, Tick to, const InputFrame& in, EventSink&) override
    {
        if (seen) seen->push_back({ to, input.kind == SignalKind::Continuous ? in.real(0, to) : in.value(0).asReal() });
        return {};
    }
    void getOutputs(OutputFrame&) const override {}
    std::vector<std::uint8_t> saveState() const override { return {}; }
    Status restoreState(const std::vector<std::uint8_t>&) override { return {}; }
    double first = 0.0;
};

// Behaviours that must fail loudly.
struct Faulty final : Participant
{
    enum class Kind { NaN, StepError, NoConvergence, Breakpoint, DependsOn };
    std::string name = "F";
    Kind kind = Kind::NaN;
    Tick at = ms(5);
    Tick step = ms(1);
    TimingKind timing = TimingKind::FixedStep;
    double out = 0.0, trial = 0.0;
    int pass = 0;
    int terminated = 0;
    std::vector<std::string> deps;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        auto in = PortSpec::continuous("in", "1");
        in.required = false;
        i.inputs = { in };
        auto o = PortSpec::continuous("out", "1");
        o.dependsOn = deps;
        i.outputs = { o };
        i.timing = { timing, step };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick, int p, const InputFrame&) override
    {
        pass = p;
        out = trial = kind == Kind::NoConvergence ? (double)p : 0.0;
        return {};
    }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink&) override
    {
        StepResult r;
        trial = 1.0;
        if (to >= at)
        {
            if (kind == Kind::NaN) trial = std::nan("");
            if (kind == Kind::StepError) r.status = Status::failure("the model's Newton iteration diverged");
            if (kind == Kind::Breakpoint && to == at) r.pauseRequested = true;
        }
        return r;
    }
    void commit() override { out = trial; }
    void rollback() override { trial = out; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, trial); }
    void terminate() override { ++terminated; }
    std::vector<std::uint8_t> saveState() const override { return bytesOf(out); }
    Status restoreState(const std::vector<std::uint8_t>& b) override { out = trial = doubleFrom(b); return {}; }
};

// A random walk with its own seeded generator (state in its saved state).
struct Walker final : Participant
{
    std::string name = "W";
    Random rng;
    std::uint64_t seed = 0;
    double v = 0.0, trial = 0.0;
    Random trialRng;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::continuous("v", "1") };
        i.timing = { TimingKind::FixedStep, ms(0.5) };
        i.caps.canRollback = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status configure(const ParticipantConfig& c) override { seed = c.seed; return {}; }
    Status initialize(Tick, int, const InputFrame&) override { rng.reseed(seed); trialRng = rng; v = trial = 0.0; return {}; }
    StepResult doStep(Tick, Tick, const InputFrame&, EventSink&) override
    {
        trialRng = rng;
        trial = v + (trialRng.uniform() - 0.5);
        return {};
    }
    void commit() override { v = trial; rng = trialRng; }
    void rollback() override { trial = v; trialRng = rng; }
    void getOutputs(OutputFrame& o) const override { o.setReal(0, trial); }
    std::vector<std::uint8_t> saveState() const override
    {
        std::vector<std::uint8_t> b(sizeof(double) + 32);
        std::memcpy(b.data(), &v, sizeof v);
        const auto s = rng.state();
        std::memcpy(b.data() + 8, s.data(), 32);
        return b;
    }
    Status restoreState(const std::vector<std::uint8_t>& b) override
    {
        std::array<std::uint64_t, 4> s {};
        std::memcpy(&v, b.data(), 8);
        std::memcpy(s.data(), b.data() + 8, 32);
        rng.setState(s);
        trialRng = rng;
        trial = v;
        return {};
    }
};

// Emits an event every `period` (variable step + next event time).
struct Ticker final : Participant
{
    std::string name = "T";
    Tick period = ms(0.1);
    Tick next = 0, trialNext = 0;
    std::int64_t count = 0, trialCount = 0;
    ParticipantInfo describe() const override
    {
        ParticipantInfo i;
        i.name = name;
        i.outputs = { PortSpec::event("tick") };
        i.timing = { TimingKind::VariableStep, sec(1), 1 };
        i.caps.canRollback = true;
        i.caps.providesNextEventTime = true;
        i.caps.stateSerializable = true;
        return i;
    }
    Status initialize(Tick t0, int, const InputFrame&) override { next = trialNext = t0 + period; count = trialCount = 0; return {}; }
    Tick nextEventTime() const override { return next; }
    StepResult doStep(Tick, Tick to, const InputFrame&, EventSink& sink) override
    {
        trialNext = next;
        trialCount = count;
        if (to == next)
        {
            ++trialCount;
            sink.emit(0, to, { trialCount % 10 == 0 ? 1002 : 100000 + trialCount, 0.0 }); // every 10th starts a two-step cascade
            trialNext = next + period;
        }
        return {};
    }
    void commit() override { next = trialNext; count = trialCount; }
    void rollback() override { trialNext = next; trialCount = count; }
    void getOutputs(OutputFrame&) const override {}
    std::vector<std::uint8_t> saveState() const override
    {
        std::vector<std::uint8_t> b(16);
        std::memcpy(b.data(), &next, 8);
        std::memcpy(b.data() + 8, &count, 8);
        return b;
    }
    Status restoreState(const std::vector<std::uint8_t>& b) override
    {
        std::memcpy(&next, b.data(), 8);
        std::memcpy(&count, b.data() + 8, 8);
        trialNext = next;
        trialCount = count;
        return {};
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string joined(const std::vector<std::string>& lines)
{
    std::string s;
    for (const auto& l : lines) s += l + "\n";
    return s;
}

void lifecycle(const std::string& what, const Simulation& s) { lifecycleLog.push_back(what + " -> " + toString(s.state())); }

// ---- 1. time ------------------------------------------------------------------

void testTime()
{
    std::printf("-- clock range and overflow (F1) --\n");
    Tick t = 0;
    check(secondsToTicks(1.0, t) && t == kTicksPerSecond, "1 s = 10^12 ticks");
    check(secondsToTicks(1e-12, t) && t == 1, "1 ps = 1 tick");
    const double days = ticksToSeconds(kLastSchedulable) / 86400.0;
    check(days > 106.7 && days < 106.8, "positive range is about 106.7 days", std::to_string(days));
    check(!secondsToTicks(1e7, t), "10^7 s (115.7 days) is refused, not wrapped");
    check(!secondsToTicks(std::nan(""), t), "NaN seconds are refused");
    check(!checkedAdd(kLastSchedulable, 1, t), "adding past the range is detected");
    check(checkedAdd(kLastSchedulable - 1, 1, t) && t == kLastSchedulable, "adding up to the last schedulable tick works");
    check(!checkedMultiply(kTicksPerSecond, 10'000'000, t), "multiplying past the range is detected");

    // A participant whose second step would cross the end of the clock.
    {
        Simulation s;
        auto p = std::make_unique<ExpDecay>();
        p->name = "A";
        p->step = sec(1000);
        s.addParticipant(std::move(p));
        s.configure();
        const Tick t0 = kLastSchedulable - sec(1500);
        check((bool)s.initialize(t0), "initialise near the end of the range (first step fits)");
        s.start();
        const auto r1 = s.advanceRound();
        const auto r2 = s.advanceRound();
        check(r1.kind == RoundOutcome::Kind::Advanced && r2.kind == RoundOutcome::Kind::Faulted && contains(s.lastError(), "beyond the clock's range"),
              "a step past the range faults with a named error, no wraparound", s.lastError());
    }
    {
        Simulation s;
        auto p = std::make_unique<ExpDecay>();
        p->name = "A";
        s.addParticipant(std::move(p));
        s.configure();
        const auto st = s.initialize(kLastSchedulable);
        check(!st && contains(st.message, "beyond the clock's range"), "initialising where the first step cannot fit is refused", st.message);
    }
    {
        Simulation s;
        auto p = std::make_unique<ExpDecay>();
        p->name = "A";
        s.addParticipant(std::move(p));
        s.addExternalSignal("ext", PortSpec::event("ext"));
        s.configure();
        s.initialize(0);
        s.start();
        const auto st = s.runUntil(kNever);
        check(!st && contains(st.message, "beyond the clock's range"), "running to 'never' is refused", st.message);
        const auto st2 = s.scheduleExternal("ext", kNever, {});
        check(!st2, "scheduling an event at 'never' is refused", st2.message);
    }
}

// ---- 2. units -----------------------------------------------------------------

void testUnits()
{
    std::printf("-- units and dimensional validation (F7) --\n");
    Unit u;
    std::string error;
    bool allParse = true;
    for (const char* s : { "rpm", "rad/s", "V", "mA", "degC", "K", "N*m", "kg*m^2", "m/s^2", "kohm", "uF", "Hz", "%", "1", "bar", "L/min" })
        allParse &= parseUnit(s, u, error);
    check(allParse, "common units parse", error);
    check(!parseUnit("furlong", u, error) && contains(error, "furlong"), "an unknown unit is refused by name", error);
    Unit rpm, rad, hz, volt, amp, degc, kelvin;
    parseUnit("rpm", rpm, error);
    parseUnit("rad/s", rad, error);
    parseUnit("Hz", hz, error);
    parseUnit("V", volt, error);
    parseUnit("A", amp, error);
    parseUnit("degC", degc, error);
    parseUnit("K", kelvin, error);
    Conversion c;
    check(conversionBetween(rpm, rad, c) && std::abs(c.value(600.0) - 62.83185307179586) < 1e-12 * 62.83, "600 rpm = 62.8319 rad/s");
    check(!conversionBetween(hz, rad, c), "Hz and rad/s do not convert (no silent 2-pi)");
    check(!conversionBetween(volt, amp, c), "V and A do not convert");
    check(conversionBetween(degc, kelvin, c) && std::abs(c.value(25.0) - 298.15) < 1e-12, "25 degC = 298.15 K (offset)");

    auto build = [](const char* inputUnit, bool convert, Status& status, std::shared_ptr<std::vector<std::pair<Tick, double>>> seen = nullptr) {
        auto s = std::make_unique<Simulation>();
        s->addExternalSignal("speed", PortSpec::continuous("speed", "rpm"));
        auto p = std::make_unique<Probe>();
        p->input = PortSpec::continuous("in", inputUnit);
        p->seen = seen;
        s->addParticipant(std::move(p));
        BindOptions o;
        o.convertUnits = convert;
        status = s->connect("speed", "P.in", o);
        return s;
    };
    Status st;
    build("rad/s", false, st);
    check(!st && contains(st.message, "explicit unit conversion"), "rpm -> rad/s without a conversion is refused", st.message);
    build("V", true, st);
    check(!st && contains(st.message, "dimensions differ"), "rpm -> V is refused even with conversion allowed", st.message);
    auto seen = std::make_shared<std::vector<std::pair<Tick, double>>>();
    auto s = build("rad/s", true, st, seen);
    check((bool)st, "rpm -> rad/s with an explicit conversion connects", st.message);
    s->configure();
    s->initialize(0);
    s->start();
    s->setExternal("speed", Value::ofReal(600.0));
    s->runUntil(ms(3));
    const double got = seen->empty() ? 0.0 : seen->back().second;
    check(std::abs(got - 62.83185307179586) <= 1e-12 * 62.83185307179586, "the input reads 600 rpm as 62.83185307179586 rad/s", std::to_string(got));

    Simulation k;
    k.addExternalSignal("ev", PortSpec::event("ev"));
    k.addParticipant(std::make_unique<Probe>());
    st = k.connect("ev", "P.in");
    check(!st && contains(st.message, "event"), "an event signal cannot feed a continuous input", st.message);
}

// ---- 3. multirate coupled ODEs ------------------------------------------------

std::unique_ptr<Simulation> coupledSystem(std::shared_ptr<std::vector<std::pair<Tick, double>>> trace, Tick maxExtrapolation = 0)
{
    auto s = std::make_unique<Simulation>();
    auto a = std::make_unique<ExpDecay>();
    a->name = "A";
    auto b = std::make_unique<Filter>();
    b->name = "B";
    b->trace = trace;
    b->maxExtrapolation = maxExtrapolation;
    s->addParticipant(std::move(a));
    s->addParticipant(std::move(b));
    s->connect("A.x", "B.u");
    return s;
}

void testCoupled()
{
    std::printf("-- two coupled analytic participants at 1 ms and 0.1 ms --\n");
    auto trace = std::make_shared<std::vector<std::pair<Tick, double>>>();
    auto s = coupledSystem(trace);
    RecordingWriter rec(path("coupled.simrec"), path("coupled.manifest.json"));
    s->setRecorder(&rec);
    s->configure();
    s->initialize(0);
    s->start();
    const auto c0 = std::chrono::steady_clock::now();
    s->runUntil(sec(2));
    const double recordedWall = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
    s->terminate();
    const auto fin = rec.finish();
    {
        auto plain = coupledSystem(nullptr);
        plain->configure(); plain->initialize(0); plain->start();
        const auto p0 = std::chrono::steady_clock::now();
        plain->runUntil(sec(2));
        const double plainWall = std::chrono::duration<double>(std::chrono::steady_clock::now() - p0).count();
        std::printf("      20,000 rounds: %.3f s with the recorder, %.3f s without (%.1f / %.1f us per round)\n", recordedWall, plainWall,
                    recordedWall / 20000 * 1e6, plainWall / 20000 * 1e6);
    }
    check((bool)fin, "recording written", fin.message);
    // y(t) = y0 e^-bt + x0 (e^-at - e^-bt) / (b - a), a = 1, b = 3, x0 = 1, y0 = 0.5
    double worst = 0.0;
    Tick worstAt = 0;
    for (const auto& [t, y] : *trace)
    {
        const double ts = ticksToSeconds(t);
        const double exact = 0.5 * std::exp(-3.0 * ts) + (std::exp(-ts) - std::exp(-3.0 * ts)) / 2.0;
        const double rel = std::abs(y - exact) / std::abs(exact);
        if (rel > worst) { worst = rel; worstAt = t; }
    }
    char detail[120];
    std::snprintf(detail, sizeof detail, "worst relative error %.3g at %.4f s over %zu steps", worst, ticksToSeconds(worstAt), trace->size());
    check(trace->size() == 20000 && worst < 1e-6, "B (0.1 ms) fed by A (1 ms) matches the closed form within 1e-6", detail);
    std::printf("      %s\n", detail);
    check(s->rounds() == 20000, "20,000 rounds: one per 0.1 ms; A steps only every 10th", std::to_string(s->rounds()));

    // The input extrapolation limit warns (a synchronisation diagnostic).
    auto trace2 = std::make_shared<std::vector<std::pair<Tick, double>>>();
    auto w = coupledSystem(trace2, ms(0.5));
    w->configure();
    w->initialize(0);
    w->start();
    w->runUntil(ms(2));
    bool warned = false;
    for (const auto& d : w->diagnostics().all())
        warned |= d.category == Category::Synchronisation && d.severity == Severity::Warning && contains(d.message, "ahead of its last value");
    check(warned, "reading an input beyond its extrapolation limit raises a synchronisation warning");
}

// ---- 4. event ordering --------------------------------------------------------

struct EventRig
{
    std::unique_ptr<Simulation> sim = std::make_unique<Simulation>();
    std::shared_ptr<std::vector<std::string>> log = std::make_shared<std::vector<std::string>>();
    Script* s1 = nullptr;
    Script* s2 = nullptr;
    Logger* logger = nullptr;

    explicit EventRig(std::vector<Script::Item> one, std::vector<Script::Item> two = {}, bool variableLogger = false, bool withProbe = false)
    {
        auto a = std::make_unique<Script>();
        a->name = "S1";
        a->items = std::move(one);
        s1 = a.get();
        auto b = std::make_unique<Script>();
        b->name = "S2";
        b->items = std::move(two);
        s2 = b.get();
        auto l = std::make_unique<Logger>();
        l->log = log;
        l->variable = variableLogger;
        logger = l.get();
        sim->addParticipant(std::move(a));
        sim->addParticipant(std::move(b));
        sim->addParticipant(std::move(l));
        sim->addExternalSignal("ext.ev", PortSpec::event("ev"));
        sim->connect("S1.a", "L.e0");
        sim->connect("S2.a", "L.e1");
        sim->connect("L.out", "L.e2");
        sim->connect("ext.ev", "L.e3");
        if (withProbe)
        {
            auto x = std::make_unique<ExpDecay>();
            x->name = "X";
            sim->addParticipant(std::move(x));
            sim->connect("X.x", "L.probe");
        }
    }
    bool run(Tick until)
    {
        if (!sim->configure() || !sim->initialize(0) || !sim->start()) return false;
        return (bool)sim->runUntil(until);
    }
};

void expectLog(const std::string& name, const std::vector<std::string>& got, const std::vector<std::string>& want)
{
    eventLog.push_back("## " + name);
    for (const auto& l : got) eventLog.push_back(l);
    check(got == want, name, "got:\n" + joined(got) + "want:\n" + joined(want));
}

void testEvents()
{
    std::printf("-- event ordering (20 scripted cases) --\n");
    const Tick t1 = ms(1), t2 = ms(2), t5 = ms(5);
    { // 1
        EventRig r({ { t2, 0, 2 }, { t1, 0, 1 } });
        std::sort(r.s1->items.begin(), r.s1->items.end(), [](auto& a, auto& b) { return a.at < b.at; });
        r.run(ms(3));
        expectLog("01 events at different ticks run in time order", *r.log,
                  { "L t=1000000000 m=0 p=0 c=1 src=0 behind", "L t=2000000000 m=0 p=0 c=2 src=0 behind" });
    }
    { // 2
        EventRig r({ { t1, 0, 10 } });
        r.sim->configure(); r.sim->initialize(0);
        r.sim->scheduleExternal("ext.ev", t1, { 20, 0.0 });
        r.sim->start(); r.sim->runUntil(ms(2));
        expectLog("02 same tick: an external input runs before a participant event (priority)", *r.log,
                  { "L t=1000000000 m=0 p=3 c=20 src=-1 behind", "L t=1000000000 m=0 p=0 c=10 src=0 behind" });
    }
    { // 3
        EventRig r({ { t1, 0, 1 } }, { { t1, 0, 2 } });
        r.run(ms(2));
        expectLog("03 same tick and priority: lower registration order first", *r.log,
                  { "L t=1000000000 m=0 p=0 c=1 src=0 behind", "L t=1000000000 m=0 p=1 c=2 src=1 behind" });
    }
    { // 4
        EventRig r({ { t1, 0, 5 }, { t1, 0, 6 }, { t1, 0, 7 } });
        r.run(ms(2));
        expectLog("04 same source and tick: emission order", *r.log,
                  { "L t=1000000000 m=0 p=0 c=5 src=0 behind", "L t=1000000000 m=0 p=0 c=6 src=0 behind", "L t=1000000000 m=0 p=0 c=7 src=0 behind" });
    }
    { // 5
        EventRig r({ { t1, 0, 1001 } });
        r.run(ms(2));
        expectLog("05 a same-tick cascade runs at the next microstep", *r.log,
                  { "L t=1000000000 m=0 p=0 c=1001 src=0 behind", "L t=1000000000 m=1 p=2 c=1000 src=2 behind" });
    }
    { // 6
        EventRig r({ { t1, 0, 1005 } });
        r.run(ms(2));
        expectLog("06 a chain of five advances the microstep each time", *r.log,
                  { "L t=1000000000 m=0 p=0 c=1005 src=0 behind", "L t=1000000000 m=1 p=2 c=1004 src=2 behind", "L t=1000000000 m=2 p=2 c=1003 src=2 behind",
                    "L t=1000000000 m=3 p=2 c=1002 src=2 behind", "L t=1000000000 m=4 p=2 c=1001 src=2 behind", "L t=1000000000 m=5 p=2 c=1000 src=2 behind" });
    }
    { // 7
        EventRig r({ { t1, 0, 1002 } }, { { t1, 0, 1102 } });
        r.run(ms(2));
        expectLog("07 two cascades at one tick interleave by microstep", *r.log,
                  { "L t=1000000000 m=0 p=0 c=1002 src=0 behind", "L t=1000000000 m=0 p=1 c=1102 src=1 behind",
                    "L t=1000000000 m=1 p=2 c=1001 src=2 behind", "L t=1000000000 m=1 p=2 c=1101 src=2 behind",
                    "L t=1000000000 m=2 p=2 c=1000 src=2 behind", "L t=1000000000 m=2 p=2 c=1100 src=2 behind" });
    }
    { // 8
        EventRig r({ { t1, 0, 3000 } });
        r.run(ms(10));
        expectLog("08 an event a handler schedules later runs in a later round at microstep 0", *r.log,
                  { "L t=1000000000 m=0 p=0 c=3000 src=0 behind", "L t=6000000000 m=0 p=2 c=3001 src=2 behind" });
    }
    { // 9
        EventRig r({ { 123456789, 0, 9 } });
        r.run(ms(1));
        expectLog("09 the round lands exactly on an odd event tick (123456789 ps)", *r.log, { "L t=123456789 m=0 p=0 c=9 src=0 behind" });
    }
    { // 10, 11
        EventRig r({ { ms(20), 0, 1 } });
        r.sim->configure(); r.sim->initialize(0); r.sim->start();
        r.sim->runUntil(ms(10));
        r.sim->pause();
        r.sim->emitExternal("ext.ev", { 55, 0.0 });
        r.sim->emitExternal("ext.ev", { 56, 0.0 });
        r.sim->start();
        r.sim->runToNextEvent();
        expectLog("10 an external input waits for the next round boundary (20 ms), before same-tick participant events", *r.log,
                  { "L t=20000000000 m=0 p=3 c=55 src=-1 behind", "L t=20000000000 m=0 p=3 c=56 src=-1 behind", "L t=20000000000 m=0 p=0 c=1 src=0 behind" });
        check(r.log->size() == 3 && contains((*r.log)[0], "c=55") && contains((*r.log)[1], "c=56"), "11 two external inputs at one boundary keep their order");
        eventLog.push_back("## 11 external order (see 10)");
    }
    { // 12
        auto s = std::make_unique<Simulation>();
        s->addExternalSignal("level", PortSpec::discrete("level", ValueType::Integer));
        auto p = std::make_unique<Probe>();
        p->input = PortSpec::discrete("in", ValueType::Integer);
        auto seen = std::make_shared<std::vector<std::pair<Tick, double>>>();
        p->seen = seen;
        s->addParticipant(std::move(p));
        s->connect("level", "P.in");
        s->configure(); s->initialize(0); s->start();
        s->runUntil(ms(10));
        s->pause();
        s->setExternal("level", Value::ofInteger(5));
        s->start();
        s->runUntil(ms(13));
        std::vector<std::string> got;
        for (const auto& [t, v] : *seen)
            if (t > ms(10)) got.push_back(std::to_string(t) + "=" + std::to_string((int)v));
        expectLog("12 an external value set at a boundary is seen from the following step", got,
                  { "11000000000=0", "12000000000=5", "13000000000=5" });
    }
    { // 13
        EventRig r({}, { { t1, 1, 77 } });
        RecordingWriter rec(path("events13.simrec"), path("events13.manifest.json"));
        r.sim->setRecorder(&rec);
        r.run(ms(2));
        r.sim->terminate();
        rec.finish();
        Recording rd;
        readRecording(path("events13.simrec"), rd);
        const int sig = r.sim->signalId("S2.b");
        const bool recorded = std::any_of(rd.events.begin(), rd.events.end(), [&](const Event& e) { return e.signal == sig && e.payload.code == 77; });
        check(recorded && r.log->empty(), "13 an event on an unconnected output is recorded and delivered to no one");
        eventLog.push_back(std::string("## 13 recorded=") + (recorded ? "1" : "0") + " delivered=" + std::to_string(r.log->size()));
    }
    { // 14
        EventRig r({});
        r.sim->configure(); r.sim->initialize(0); r.sim->start();
        r.sim->runUntil(ms(1500));
        const auto st = r.sim->scheduleExternal("ext.ev", ms(500), {});
        check(!st && contains(st.message, "not in the future"), "14 an input scheduled in the past is refused with a named error", st.message);
        eventLog.push_back("## 14 past refused");
    }
    { // 15
        EventRig r({ { t1, 0, 9999 } });
        r.sim->setCascadeLimit(50);
        r.run(ms(2));
        check(r.sim->state() == LifecycleState::Faulted && contains(r.sim->lastError(), "does not settle") && r.log->size() == 51,
              "15 a cascade past the limit faults with 'does not settle' (50 microsteps)", r.sim->lastError());
        eventLog.push_back("## 15 cascade " + std::to_string(r.log->size()));
    }
    { // 16, 17
        EventRig fixed({ { t5, 0, 1 } });
        fixed.run(ms(6));
        EventRig variable({ { t5, 0, 1 } }, {}, true);
        variable.run(ms(6));
        expectLog("16 a fixed-step subscriber gets the event while behind (between its steps)", *fixed.log, { "L t=5000000000 m=0 p=0 c=1 src=0 behind" });
        expectLog("17 a variable-step subscriber is stepped to the event time first", *variable.log, { "L t=5000000000 m=0 p=0 c=1 src=0 at" });
    }
    { // 18
        auto s = std::make_unique<Simulation>();
        auto l = std::make_unique<Locator>();
        l->name = "Loc";
        l->crossing = 345678900000;
        auto log = std::make_shared<std::vector<std::string>>();
        auto lg = std::make_unique<Logger>();
        lg->log = log;
        s->addParticipant(std::move(l));
        s->addParticipant(std::move(lg));
        s->connect("Loc.crossed", "L.e0");
        s->configure(); s->initialize(0); s->start();
        s->runUntil(sec(0.4));
        expectLog("18 a located crossing ends the round exactly at the crossing tick", *log, { "L t=345678900000 m=0 p=0 c=1 src=0 behind" });
    }
    { // 19
        auto s = std::make_unique<Simulation>();
        auto order = std::make_shared<std::vector<std::string>>();
        auto l1 = std::make_unique<Locator>();
        l1->name = "L1";
        l1->crossing = sec(0.5);
        l1->order = order;
        auto l2 = std::make_unique<Locator>();
        l2->name = "L2";
        l2->crossing = 345678900000;
        l2->order = order;
        auto* p1 = l1.get();
        auto* p2 = l2.get();
        s->addParticipant(std::move(l1));
        s->addParticipant(std::move(l2));
        RecordingWriter rec(path("locators.simrec"), path("locators.manifest.json"));
        s->setRecorder(&rec);
        s->configure(); s->initialize(0); s->start();
        s->runUntil(sec(0.36));
        std::vector<std::string> window;
        for (const auto& o : *order)
            if (contains(o, "@34") || contains(o, "@35")) window.push_back(o);
        expectLog("19 a later locator's earlier crossing rolls the first back; it steps again to the crossing", window,
                  { "L1@340000000000", "L2@340000000000", "L1@350000000000", "L2@350000000000", "L1@345678900000", "L1@355678900000", "L2@355678900000" });
        check(p1->rollbacks == 1 && p2->rollbacks == 0, "19 exactly one rollback (L1)", std::to_string(p1->rollbacks) + "," + std::to_string(p2->rollbacks));
        s->terminate();
        rec.finish();
        Recording rd;
        readRecording(path("locators.simrec"), rd);
        const int x1 = s->signalId("L1.x");
        bool trialLeaked = false;
        for (const auto& smp : rd.samples[x1])
            trialLeaked |= smp.tick == sec(0.35);
        check(!trialLeaked, "rollback leaves no trace: L1's abandoned trial at 0.35 s is not in the recording");
    }
    { // 20
        EventRig r({ { t5, 0, 7777 } }, {}, false, true);
        r.run(ms(6));
        const double expect = std::exp(-0.005);
        char want[200];
        std::snprintf(want, sizeof want, "L t=5000000000 m=0 p=0 c=7777 src=0 behind probe=%.17g", expect);
        expectLog("20 events at a tick run after that tick's commits (the handler sees X at 5 ms)", *r.log, { want });
    }
}

// ---- 5. replay ----------------------------------------------------------------

std::unique_ptr<Simulation> replaySystem(std::shared_ptr<std::vector<std::string>> log)
{
    auto s = std::make_unique<Simulation>();
    auto t = std::make_unique<Ticker>();
    auto l = std::make_unique<Logger>();
    l->log = log;
    auto w = std::make_unique<Walker>();
    ParticipantConfig wc;
    wc.seed = 42;
    s->addParticipant(std::move(t));
    s->addParticipant(std::move(l));
    s->addParticipant(std::move(w), wc);
    auto a = std::make_unique<ExpDecay>();
    a->name = "A";
    auto b = std::make_unique<Filter>();
    b->name = "B";
    s->addParticipant(std::move(a));
    s->addParticipant(std::move(b));
    s->addExternalSignal("ext.ev", PortSpec::event("ev"));
    s->addExternalSignal("ext.level", PortSpec::discrete("level", ValueType::Integer));
    s->connect("T.tick", "L.e0");
    s->connect("L.out", "L.e2");
    s->connect("ext.ev", "L.e3");
    s->connect("A.x", "B.u");
    return s;
}

void testReplay()
{
    std::printf("-- deterministic replay --\n");
    auto logA = std::make_shared<std::vector<std::string>>();
    auto a = replaySystem(logA);
    RecordingWriter recA(path("replay_a.simrec"), path("replay_a.manifest.json"));
    a->setRecorder(&recA);
    a->configure(); a->initialize(0); a->start();
    for (int k = 1; k <= 10; ++k)
    {
        if (auto st = a->runUntil(ms(100.0 * k)); !st)
            std::printf("      run to %d ms failed: %s\n", 100 * k, st.message.c_str());
        a->pause();
        a->setExternal("ext.level", Value::ofInteger(k));
        a->emitExternal("ext.ev", { 500 + k, 0.0 });
        a->start();
    }
    const auto lastRun = a->runUntil(sec(1.05));
    check((bool)lastRun && a->state() == LifecycleState::Running && a->now().tick == sec(1.05), "the original run reaches 1.05 s without a fault",
          lastRun.message + " state " + toString(a->state()) + " at " + describeTick(a->now().tick));
    a->terminate();
    check((bool)recA.finish(), "original run recorded");

    Recording ra;
    readRecording(path("replay_a.simrec"), ra);
    check(ra.events.size() >= 10000, "the run has at least 10,000 events", std::to_string(ra.events.size()));
    const auto inputs = ra.externalInputs();
    std::string ticks;
    for (const auto& e : inputs) ticks += std::to_string(e.time.tick) + (e.flags & Event::ValueSet ? "v " : "e ");
    check(inputs.size() == 20, "20 external inputs were recorded", std::to_string(inputs.size()) + ": " + ticks);

    auto logB = std::make_shared<std::vector<std::string>>();
    auto b = replaySystem(logB);
    RecordingWriter recB(path("replay_b.simrec"), path("replay_b.manifest.json"));
    b->setRecorder(&recB);
    b->configure(); b->initialize(0);
    for (const auto& e : inputs)
        b->queueRecordedExternal(e);
    b->start();
    b->runUntil(sec(1.05));
    b->terminate();
    recB.finish();
    std::string diff;
    check(filesIdentical(path("replay_a.simrec"), path("replay_b.simrec"), diff), "replay A reproduces the recording bit for bit", diff);
    check(*logA == *logB, "replay delivers the same events to the same handlers in the same order");
}

// ---- 6. recorder format -------------------------------------------------------

void testRecorder()
{
    std::printf("-- recording format v1 (F8) --\n");
    Recording r;
    auto st = readRecording(path("coupled.simrec"), r);
    check(st && r.major == 1 && r.minor == 0 && r.complete && r.ticksPerSecond == kTicksPerSecond, "v1 recording reads back complete with its tick resolution", st.message);
    std::size_t samples = 0;
    for (const auto& [sig, v] : r.samples) samples += v.size();
    check(samples == 2 + 2000 + 20000, "every committed sample is present (2 initial + 2,000 A + 20,000 B)", std::to_string(samples));

    std::ifstream in(path("coupled.simrec"), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    {
        auto bad = bytes;
        bad[24 + 8 + 100] ^= 0x5A;
        std::ofstream(path("corrupt.simrec"), std::ios::binary).write(bad.data(), (std::streamsize)bad.size());
        Recording x;
        const auto e = readRecording(path("corrupt.simrec"), x);
        check(!e && contains(e.message, "chunk 0") && contains(e.message, "corrupted"), "a corrupted chunk is detected and named", e.message);
    }
    {
        auto newer = bytes;
        newer[8] = 2;
        std::ofstream(path("newer.simrec"), std::ios::binary).write(newer.data(), (std::streamsize)newer.size());
        Recording x;
        const auto e = readRecording(path("newer.simrec"), x);
        check(!e && contains(e.message, "newer than this reader"), "a newer major version is refused with a clear message", e.message);
    }
    {
        // A hand-made file whose samples go back in time (valid checksum).
        std::vector<std::uint8_t> f = { 'W', 'B', 'S', 'I', 'M', 'R', 'E', 'C', 1, 0, 0, 0, 0, 0, 0, 0 };
        auto put = [&](auto v) { const auto at = f.size(); f.resize(at + sizeof v); std::memcpy(f.data() + at, &v, sizeof v); };
        put((std::int64_t)kTicksPerSecond);
        std::vector<std::uint8_t> payload;
        auto pp = [&](auto v) { const auto at = payload.size(); payload.resize(at + sizeof v); std::memcpy(payload.data() + at, &v, sizeof v); };
        pp((std::uint32_t)0); pp((std::uint32_t)0); pp((std::uint32_t)2);
        pp((std::int64_t)5); pp((std::int64_t)3);
        pp((std::uint32_t)0); pp((std::uint32_t)0);
        pp(1.0); pp(2.0);
        std::vector<std::uint8_t> head;
        auto ph = [&](auto v) { const auto at = head.size(); head.resize(at + sizeof v); std::memcpy(head.data() + at, &v, sizeof v); };
        ph((std::uint32_t)1); ph((std::uint32_t)payload.size());
        std::uint32_t crc = recordingCrc32(head.data(), head.size());
        crc = recordingCrc32(payload.data(), payload.size(), crc);
        f.insert(f.end(), head.begin(), head.end());
        f.insert(f.end(), payload.begin(), payload.end());
        put(crc);
        std::ofstream(path("backwards.simrec"), std::ios::binary).write((const char*)f.data(), (std::streamsize)f.size());
        Recording x;
        const auto e = readRecording(path("backwards.simrec"), x);
        check(!e && contains(e.message, "back in time"), "timestamps that go back are rejected on read", e.message);
    }
    {
        RecordingWriter w(path("writer_backwards.simrec"), path("writer_backwards.manifest.json"));
        RunDescription run;
        SignalInfo s;
        s.id = 0;
        s.name = "x";
        run.signals.push_back(s);
        w.begin(run);
        RoundRecord r1, r2;
        r1.samples.push_back({ 0, { 10, 0 }, Value::ofReal(1.0) });
        r2.samples.push_back({ 0, { 5, 0 }, Value::ofReal(2.0) });
        w.round(std::move(r1));
        w.round(std::move(r2));
        const auto e = w.finish();
        check(!e && contains(e.message, "must not decrease"), "timestamps that go back are rejected on write", e.message);
    }
    {
        Json m;
        auto e = loadManifest(path("coupled.manifest.json"), m);
        check(e && m.find("schemaVersion") && m.find("tickResolution") && m.find("signals"), "the manifest carries schema version, tick resolution and signals", e.message);
        m.set("futureField", Json::string("kept"));
        saveManifest(path("manifest_roundtrip.json"), m);
        Json back;
        loadManifest(path("manifest_roundtrip.json"), back);
        const Json* kept = back.find("futureField");
        check(kept && kept->asString() == "kept" && back.find("signals")->items().size() == 2, "unknown manifest fields are preserved through load and save");
        Json newer = back;
        newer.set("schemaVersion", Json::integer(kManifestSchemaVersion + 1));
        saveManifest(path("manifest_newer.json"), newer);
        Json n;
        e = loadManifest(path("manifest_newer.json"), n);
        check(!e && contains(e.message, "newer than this reader"), "a newer manifest schema is refused", e.message);
        e = exportCsv(r, m, path("coupled.csv"));
        std::ifstream csv(path("coupled.csv"));
        std::string first, line;
        std::getline(csv, first);
        std::getline(csv, line);
        std::size_t rows = 0;
        std::string l;
        while (std::getline(csv, l)) if (!l.empty() && l[0] != '#') ++rows;
        check(e && contains(line, "tick resolution") && rows == samples + 1, "CSV export states the tick resolution and has one row per sample", std::to_string(rows));
    }
}

// ---- 7. lifecycle -------------------------------------------------------------

void testLifecycle()
{
    std::printf("-- participant lifecycle --\n");
    {
        Simulation s;
        s.addParticipant(std::make_unique<Probe>());
        lifecycle("new", s);
        const auto st = s.configure();
        lifecycle("configure with an unconnected required input", s);
        check(!st && contains(st.message, "P.in is required but not connected") && s.state() == LifecycleState::Created,
              "Created: configuration with an unbound required input is refused and stays Created", st.message);
    }
    {
        auto loop = [](TimingKind second) {
            auto s = std::make_unique<Simulation>();
            auto f1 = std::make_unique<Faulty>();
            f1->name = "F1";
            f1->deps = { "in" };
            auto f2 = std::make_unique<Faulty>();
            f2->name = "F2";
            f2->deps = { "in" };
            f2->timing = second;
            s->addParticipant(std::move(f1));
            s->addParticipant(std::move(f2));
            s->connect("F1.out", "F2.in");
            s->connect("F2.out", "F1.in");
            return s;
        };
        auto a = loop(TimingKind::FixedStep);
        const auto st = a->configure();
        check(!st && contains(st.message, "algebraic loop"), "a direct-feedthrough loop across continuous participants is refused (C2)", st.message);
        auto b = loop(TimingKind::Sampled);
        check((bool)b->configure(), "the same loop through a sampled participant is accepted (C2)", b->lastError());
    }
    {
        Simulation s;
        auto f = std::make_unique<Faulty>();
        f->name = "Osc";
        f->kind = Faulty::Kind::NoConvergence;
        s.addParticipant(std::move(f));
        s.configure();
        lifecycle("configure", s);
        const auto st = s.initialize(0);
        lifecycle("initialise a participant that never settles", s);
        check(!st && contains(st.message, "did not converge") && contains(st.message, "Osc") && s.state() == LifecycleState::Configured,
              "initialisation that does not converge names the participant and stays Configured", st.message);
    }

    auto make = [](Faulty::Kind kind, Faulty** out) {
        auto s = std::make_unique<Simulation>();
        auto a = std::make_unique<ExpDecay>();
        a->name = "A";
        auto f = std::make_unique<Faulty>();
        f->kind = kind;
        *out = f.get();
        s->addParticipant(std::move(a));
        s->addParticipant(std::move(f));
        return s;
    };
    {
        Faulty* f = nullptr;
        auto s = make(Faulty::Kind::Breakpoint, &f);
        check(s->advanceRound().kind == RoundOutcome::Kind::Refused, "Created: advancing is refused");
        s->configure();
        lifecycle("configure", *s);
        check(s->addParticipant(std::make_unique<Probe>()) == -1 && s->state() == LifecycleState::Configured, "Configured: adding a participant is refused");
        check(!s->connect("A.x", "F.in"), "Configured: connecting is refused");
        s->initialize(0);
        lifecycle("initialise", *s);
        check(s->state() == LifecycleState::Initialized, "Configured -> Initialized");
        s->start();
        lifecycle("start", *s);
        check(s->state() == LifecycleState::Running, "Initialized -> Running");
        s->runUntil(ms(10));
        lifecycle("breakpoint at 5 ms", *s);
        check(s->state() == LifecycleState::Paused && s->now().tick == ms(5), "Running -> Paused at a participant's breakpoint (5 ms)", describeTick(s->now().tick));
        s->start();
        lifecycle("resume", *s);
        check(s->state() == LifecycleState::Running, "Paused -> Running");
        s->runUntil(ms(8));
        s->pause();
        lifecycle("pause", *s);
        check(s->state() == LifecycleState::Paused, "Running -> Paused (pause)");
        const auto values1 = s->committedValue(s->signalId("A.x")).real;
        s->reset();
        lifecycle("reset", *s);
        check(s->state() == LifecycleState::Initialized && s->now().tick == 0, "Paused -> Initialized (reset to t0)");
        s->start();
        s->runUntil(ms(10)); // breakpoint again at 5 ms
        s->start();
        s->runUntil(ms(8));
        check(s->committedValue(s->signalId("A.x")).real == values1, "after reset the run reproduces the same values bit for bit");
        s->pause();
        s->terminate();
        lifecycle("terminate", *s);
        check(s->state() == LifecycleState::Terminated && f->terminated == 1, "Paused -> Terminated calls terminate on every participant");
    }
    {
        Faulty* f = nullptr;
        auto s = make(Faulty::Kind::NaN, &f);
        s->configure(); s->initialize(0); s->start();
        s->runUntil(ms(10));
        lifecycle("a participant outputs NaN", *s);
        check(s->state() == LifecycleState::Faulted && s->participantState(1) == LifecycleState::Faulted && contains(s->lastError(), "F: output out is not finite")
                  && s->now().tick == ms(4),
              "Running -> Faulted on a non-finite output, naming participant and port; nothing committed past 4 ms", s->lastError());
        check(s->committedValue(s->signalId("F.out")).real == 1.0, "the faulted round's NaN never reached the bus");
        s->reset();
        lifecycle("reset after fault", *s);
        check(s->state() == LifecycleState::Initialized && s->participantState(1) == LifecycleState::Initialized, "Faulted -> Initialized (reset)");
        s->start();
        s->runUntil(ms(2));
        s->terminate();
        lifecycle("terminate while running", *s);
        check(s->state() == LifecycleState::Terminated, "Running -> Terminated");
    }
    {
        Faulty* f = nullptr;
        auto s = make(Faulty::Kind::StepError, &f);
        s->configure(); s->initialize(0); s->start();
        s->runUntil(ms(10));
        check(s->state() == LifecycleState::Faulted && contains(s->lastError(), "Newton iteration diverged") && contains(s->lastError(), "F:"),
              "a participant's step error faults the run with its reason", s->lastError());
        bool diag = false;
        for (const auto& d : s->diagnostics().all())
            diag |= d.severity == Severity::Error && d.participant == "F";
        check(diag, "the fault is an error diagnostic naming the participant");
    }
    {
        // C1: the participant that cannot roll back steps last, after the
        // locator has settled the round, and is never rolled back.
        auto s = std::make_unique<Simulation>();
        auto order = std::make_shared<std::vector<std::string>>();
        auto nr = std::make_unique<NoRollback>();
        nr->order = order;
        nr->step = ms(5);
        auto* nrp = nr.get();
        auto l = std::make_unique<Locator>();
        l->name = "Loc";
        l->crossing = ms(17);
        l->maxStep = ms(10);
        l->order = order;
        s->addParticipant(std::move(nr)); // registered first, still stepped last
        s->addParticipant(std::move(l));
        s->configure(); s->initialize(0); s->start();
        s->runUntil(ms(20));
        // At 20 ms both are due; the locator stops at 17 ms, so NR (due at 20 ms)
        // is not stepped in that round at all, and steps to 20 ms in the next.
        expectLog("C1 non-rollback participants step after the event locator settles each round", *order,
                  { "NR@5000000000", "Loc@10000000000", "NR@10000000000", "NR@15000000000", "Loc@20000000000", "NR@20000000000" });
        check(nrp->rollbacks == 0, "C1 the non-rollback participant was never rolled back");
    }
    {
        // Checkpoint mid-run and restore: continuing gives the same result.
        auto trace = std::make_shared<std::vector<std::pair<Tick, double>>>();
        auto s = coupledSystem(trace);
        s->configure(); s->initialize(0); s->start();
        s->runUntil(ms(300));
        Status cs;
        auto cp = s->checkpoint(cs);
        s->runUntil(ms(600));
        const double first = s->committedValue(s->signalId("B.y")).real;
        s->pause();
        const auto rs = s->restore(*cp);
        s->start();
        s->runUntil(ms(600));
        check(cs && rs && s->committedValue(s->signalId("B.y")).real == first, "a checkpoint restored mid-run continues bit for bit", cs.message + rs.message);
    }
}

// ---- 8. execution modes -------------------------------------------------------

void runMode(const std::string& name, const std::function<void(Runner&)>& drive)
{
    auto log = std::make_shared<std::vector<std::string>>();
    auto s = replaySystem(log);
    RecordingWriter rec(path("mode_" + name + ".simrec"), path("mode_" + name + ".manifest.json"));
    s->setRecorder(&rec);
    s->configure();
    s->initialize(0);
    {
        Runner runner(*s);
        drive(runner);
        runner.waitIdle();
        const auto st = runner.status();
        if (name.rfind("paced", 0) == 0)
            std::printf("      %s: requested %.2fx, actual %.3fx, overruns %lld, max lag %.3f ms\n", name.c_str(), st.requestedFactor, st.actualFactor,
                        (long long)st.overruns, st.maxLagSeconds * 1e3);
    }
    s->terminate();
    rec.finish();
}

void testModes()
{
    std::printf("-- execution modes give identical results --\n");
    const Tick end = ms(300);
    const auto wall0 = std::chrono::steady_clock::now();
    runMode("free", [&](Runner& r) { r.runFree(end); });
    std::printf("      free run of 0.3 s simulated took %.3f s of wall time\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count());
    runMode("paced1", [&](Runner& r) { r.runPaced(1.0, end); });
    runMode("paced025", [&](Runner& r) { r.runPaced(0.25, end); });
    runMode("step", [&](Runner& r) {
        for (int i = 0; i < 50; ++i) r.stepRound();
        for (int i = 0; i < 5; ++i) r.stepToNextEvent();
        r.stepUntil([](const Simulation& s) { return s.now().tick >= ms(200); }, end);
        r.stepToTime(end);
    });
    std::string diff;
    check(filesIdentical(path("mode_free.simrec"), path("mode_paced1.simrec"), diff), "free run and paced 1.0x record identical results", diff);
    check(filesIdentical(path("mode_free.simrec"), path("mode_paced025.simrec"), diff), "free run and paced 0.25x record identical results", diff);
    check(filesIdentical(path("mode_free.simrec"), path("mode_step.simrec"), diff), "free run and step mode record identical results", diff);

    // Snapshots: a reader thread sees the latest committed state without blocking the scheduler.
    auto log = std::make_shared<std::vector<std::string>>();
    auto s = replaySystem(log);
    s->configure();
    s->initialize(0);
    std::atomic<bool> stop { false };
    std::atomic<int> seen { 0 };
    std::atomic<bool> ordered { true };
    std::thread reader([&] {
        Tick last = -1;
        while (!stop)
        {
            if (s->snapshots().update())
            {
                const auto& snap = s->snapshots().front();
                if (snap.tick < last) ordered = false;
                last = snap.tick;
                ++seen;
            }
            std::this_thread::yield();
        }
    });
    {
        Runner runner(*s);
        runner.runFree(ms(300));
        runner.waitIdle();
    }
    stop = true;
    reader.join();
    check(seen > 0 && ordered, "a snapshot reader sees committed state in time order while the run proceeds", std::to_string(seen.load()) + " snapshots read");
}

// ---- cross-configuration comparison --------------------------------------------

int compareDirs(const std::string& a, const std::string& b)
{
    std::printf("-- optimised vs unoptimised (%s vs %s) --\n", a.c_str(), b.c_str());
    for (const char* f : { "events.log", "lifecycle.log" })
    {
        std::string diff;
        check(filesIdentical((fs::path(a) / f).string(), (fs::path(b) / f).string(), diff), std::string(f) + " identical across configurations", diff);
    }
    for (const char* f : { "coupled.simrec", "replay_a.simrec", "mode_free.simrec", "locators.simrec" })
    {
        Recording x, y;
        const auto sa = readRecording((fs::path(a) / f).string(), x);
        const auto sb = readRecording((fs::path(b) / f).string(), y);
        bool same = sa && sb && x.samples.size() == y.samples.size() && x.events.size() == y.events.size();
        double worst = 0.0;
        for (const auto& [sig, vx] : x.samples)
        {
            const auto& vy = y.samples[sig];
            if (vx.size() != vy.size()) { same = false; break; }
            for (std::size_t k = 0; k < vx.size(); ++k)
            {
                if (vx[k].tick != vy[k].tick || vx[k].microstep != vy[k].microstep || vx[k].value.type != vy[k].value.type) { same = false; break; }
                if (vx[k].value.type == ValueType::Real)
                {
                    const double d = std::abs(vx[k].value.real - vy[k].value.real);
                    const double tol = 1e-9 * std::abs(vx[k].value.real) + 1e-12;
                    worst = std::max(worst, d);
                    if (d > tol) same = false;
                }
                else if (vx[k].value.integer != vy[k].value.integer) same = false;
            }
        }
        for (std::size_t k = 0; same && k < x.events.size(); ++k)
            same = x.events[k].time == y.events[k].time && x.events[k].signal == y.events[k].signal && x.events[k].payload.code == y.events[k].payload.code;
        char detail[96];
        std::snprintf(detail, sizeof detail, "largest difference %.3g", worst);
        check(same, std::string(f) + ": same times and event order, values within 1e-9 relative", detail);
        std::printf("      %s: %s\n", f, detail);
    }
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    if (argc >= 4 && std::string(argv[1]) == "--compare")
        return compareDirs(argv[2], argv[3]);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--out")
            outDir = argv[i + 1];
    fs::create_directories(outDir);
    std::printf("sim_core P0 tests, library configuration: %s, output: %s\n", SIM_CORE_TESTS_CONFIG, outDir.c_str());

    testTime();
    testUnits();
    testCoupled();
    testEvents();
    testReplay();
    testRecorder();
    testLifecycle();
    testModes();

    std::ofstream(path("events.log"), std::ios::binary) << joined(eventLog);
    std::ofstream(path("lifecycle.log"), std::ios::binary) << joined(lifecycleLog);
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}

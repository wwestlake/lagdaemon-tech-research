#pragma once

// The master clock and event scheduler (architecture sections 03, 05, 06,
// 11; decisions F1, F4-F7; conflicts C1, C2, C5, C8, C9).
//
// One Simulation runs on one thread (F5). It owns the clock, the event queue,
// the bus of committed signal values and the participants' lifecycle; each
// participant owns its own state. The engine never reads the wall clock:
// real time is a pacing mode applied by Runner on top.
//
// A round:
//  1. t_next = earliest of: each participant's due time (fixed and sampled:
//     last commit + step; variable: last commit + max step, or its
//     nextEventTime), and the earliest queued time-driving event.
//  2. Due participants step to t_next in a fixed order: event locators first
//     (a locator that stops early at t_e shortens the round to t_e; locators
//     already stepped roll back and step again), then other rollback-capable
//     participants, then participants that cannot roll back (C1).
//  3. All trials are checked (finite outputs, events not in the past) and
//     committed together; outputs reach the bus with their commit time.
//  4. Events at t_next run in total order (tick, microstep, priority, source,
//     sequence); handlers may emit same-tick events, which run at the next
//     microstep, up to the cascade limit.
//  5. The round is recorded and a snapshot published.
// Inputs read committed values only, so a participant never sees another's
// trial: rollback leaves no trace.

#include "sim_core/Diagnostics.h"
#include "sim_core/Participant.h"
#include "sim_core/Signals.h"
#include "sim_core/Status.h"
#include "sim_core/Time.h"
#include "sim_core/TripleBuffer.h"
#include "sim_core/Units.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sim
{
enum class LifecycleState { Created, Configured, Initialized, Running, Paused, Faulted, Terminated };
const char* toString(LifecycleState s);

struct BindOptions
{
    bool convertUnits = false; // allow a compatible unit with a different scale/offset (rpm -> rad/s)
};

struct SignalInfo
{
    int id = -1;
    std::string name;     // "participant.port", or an external signal's name
    PortSpec spec;
    Unit unit;
    int writer = -1;      // participant index; -1 = external (commands, tests, UI, agent)
};

struct ParticipantSummary
{
    std::string name;
    int order = 0;
    Timing timing;
    Capabilities caps;
    std::uint64_t seed = 0;
};

struct RunDescription
{
    std::vector<SignalInfo> signals;
    std::vector<ParticipantSummary> participants;
    Tick t0 = 0;
};

struct RoundRecord
{
    struct Sample
    {
        int signal = -1;
        SimTime time;
        Value value;
    };
    std::uint64_t round = 0;
    SimTime time;
    std::vector<Sample> samples;          // committed values written this round
    std::vector<Event> events;            // events processed this round, in order
    std::vector<Diagnostic> diagnostics;  // deterministic diagnostics raised this round
};

// Receives every committed round (the recorder). Called on the scheduler
// thread; implementations must not block it (the recorder hands off to its
// writer thread).
class RecordingSink
{
public:
    virtual ~RecordingSink() = default;
    virtual void begin(const RunDescription& run) = 0;
    virtual void round(RoundRecord&& record) = 0;
    virtual void end() = 0;
};

struct Snapshot
{
    Tick tick = 0;
    std::uint64_t round = 0;
    std::vector<Value> values; // by signal id
};

struct RoundOutcome
{
    enum class Kind { Advanced, Idle, Paused, Faulted, Refused };
    Kind kind = Kind::Refused;
    SimTime time;
    int eventsProcessed = 0;
};

class Simulation
{
public:
    Simulation();
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // ---- building (Created only) ----
    // Returns the participant's index, or -1 (see lastError()).
    int addParticipant(std::unique_ptr<Participant> participant, ParticipantConfig config = {});
    // A signal written from outside the model: commands, test inputs, UI edits.
    Status addExternalSignal(const std::string& name, PortSpec spec);
    // Binds an output signal ("p.port" or an external name) to an input ("q.port").
    Status connect(const std::string& output, const std::string& input, BindOptions options = {});

    // ---- lifecycle ----
    Status configure();
    Status initialize(Tick t0);
    Status start();     // Initialized/Paused -> Running
    Status pause();     // Running -> Paused (between rounds)
    Status terminate(); // any state but Terminated -> Terminated
    Status reset();     // Paused/Faulted/Initialized -> Initialized at t0 (needs serializable state)

    // ---- running (Running only) ----
    RoundOutcome advanceRound();
    // Rounds while the next round ends at or before `t`; stops on pause or fault.
    Status runUntil(Tick t);
    // Rounds until one processes at least one event (or the next round would end after `limit`).
    Status runToNextEvent(Tick limit = kLastSchedulable);
    // Rounds until `condition` holds after a round (checked on committed state).
    Status runUntilCondition(const std::function<bool(const Simulation&)>& condition, Tick limit);
    Tick peekNextRoundTime() const;

    // ---- external inputs (Initialized, Running, Paused) ----
    // Applied at the next round boundary and recorded (C8).
    Status setExternal(const std::string& signal, Value value);
    Status emitExternal(const std::string& signal, EventPayload payload);
    // At a stated time (drives the clock); for test scripts.
    Status scheduleExternal(const std::string& signal, Tick at, EventPayload payload);
    // Replay: re-applies recorded external inputs exactly as recorded.
    Status queueRecordedExternal(const Event& recorded);

    // ---- state ----
    struct Checkpoint;
    std::shared_ptr<const Checkpoint> checkpoint(Status& status) const;
    Status restore(const Checkpoint& checkpoint);

    // ---- inspection ----
    LifecycleState state() const { return state_; }
    LifecycleState participantState(int index) const;
    SimTime now() const { return clock_; }
    std::uint64_t rounds() const { return roundCount_; }
    const std::vector<SignalInfo>& signals() const { return signals_; }
    int signalId(const std::string& name) const;
    Value committedValue(int signal) const;
    int participantCount() const { return (int)participants_.size(); }
    const ParticipantInfo& participantInfo(int index) const;
    Participant& participant(int index);
    const DiagnosticLog& diagnostics() const { return diagnostics_; }
    DiagnosticLog& diagnostics() { return diagnostics_; }
    const std::string& lastError() const { return lastError_; }

    void setRecorder(RecordingSink* sink) { recorder_ = sink; }
    TripleBuffer<Snapshot>& snapshots() { return snapshots_; }

    void setCascadeLimit(int microsteps) { cascadeLimit_ = microsteps; }
    void setInitializationPassLimit(int passes) { initPassLimit_ = passes; }
    void setLocatorIterationLimit(int iterations) { locatorLimit_ = iterations; }

private:
    struct Binding
    {
        int signal = -1;
        Conversion conversion;
    };
    struct PendingEmission
    {
        int port = -1;
        Tick at = 0;
        EventPayload payload;
    };
    struct Entry
    {
        std::unique_ptr<Participant> participant;
        ParticipantInfo info;
        ParticipantConfig config;
        std::vector<Binding> inputs;   // per input port
        std::vector<int> outputs;      // signal id per output port
        Tick lastCommit = 0;
        bool faulted = false;
        std::vector<PendingEmission> trialEvents;
        OutputFrame trialOutputs;
    };
    struct Committed
    {
        Value value;
        double d1 = 0.0, d2 = 0.0;
        Tick at = 0;
    };
    class Sink;

    Status fail(Category category, const std::string& participant, const std::string& message);
    void note(Severity severity, Category category, const std::string& participant, const std::string& message);
    Status refuseUnless(bool allowed, const char* action);
    int findParticipant(const std::string& name) const;
    Status validateTopology();
    Status checkFeedthroughLoops();
    void buildFrame(int index, Tick at, InputFrame& frame);
    Status readOutputs(int index, bool trial);
    void publishOutputs(int index, Tick continuousAt, Tick discreteAt, std::uint32_t microstep, bool onlyChanged);
    Tick dueTime(int index) const;
    Status pushEvent(Event e);
    RoundOutcome faultRound(const std::string& participant, Category category, const std::string& message);
    Status processEvents(Tick t, int& processed);
    void publishSnapshot();
    void beginRecording();
    void resetRound();

    LifecycleState state_ = LifecycleState::Created;
    std::vector<Entry> participants_;
    std::vector<SignalInfo> signals_;
    std::vector<Committed> committed_;
    std::vector<std::vector<std::pair<int, int>>> subscribers_; // per signal: (participant, input port)
    std::set<Event, EventOrder> queue_;            // time-driving events
    std::vector<Event> pendingBoundary_;           // external inputs waiting for the next round boundary
    std::vector<Event> recordedBoundary_;          // replay: boundary-stamped inputs at recorded times
    std::uint64_t sequence_ = 0;
    SimTime clock_;
    Tick t0_ = 0;
    std::uint64_t roundCount_ = 0;
    std::shared_ptr<const Checkpoint> t0Checkpoint_;
    DiagnosticLog diagnostics_;
    std::string lastError_;
    RecordingSink* recorder_ = nullptr;
    bool recordingStarted_ = false;
    RoundRecord current_;
    // Per-round scratch, reused so a round in steady state allocates nothing.
    std::vector<Tick> scratchDue_;
    std::vector<char> scratchFirstDue_, scratchIn_;
    std::vector<int> scratchList_, scratchStepped_;
    InputFrame scratchFrame_, eventFrame_;
    std::vector<PendingEmission> stepEmissions_, eventEmissions_;
    TripleBuffer<Snapshot> snapshots_;
    int cascadeLimit_ = 1000;
    int initPassLimit_ = 50;
    int locatorLimit_ = 64;
};

struct Simulation::Checkpoint
{
    struct ParticipantState
    {
        std::vector<std::uint8_t> blob;
        Tick lastCommit = 0;
    };
    SimTime clock;
    std::uint64_t rounds = 0;
    std::uint64_t sequence = 0;
    std::vector<ParticipantState> participants;
    std::vector<Committed> committed;
    std::vector<Event> queue;
    std::vector<Event> pendingBoundary;
    std::vector<Event> recordedBoundary;
};
}

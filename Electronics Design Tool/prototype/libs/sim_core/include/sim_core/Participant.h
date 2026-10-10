#pragma once

// The participant interface (architecture sections 04-05, decision F4).
//
// Any simulation engine joins by implementing Participant: an electrical
// solver, a mechanical model, a logic engine, a software controller, a test
// sequencer, a future thermal or hydraulic model, or a hardware I/O bridge.
// The interface says nothing about physical domain or control method, and no
// participant is mandatory. Its shape follows FMI 3.0 co-simulation
// (describe, initialise in passes, step, get outputs, get/set state, event
// handling) so an FMU adapter can later be one more participant.
//
// Contract
// - doStep(from, to) is a trial: getOutputs() then reports the trial outputs,
//   but nothing reaches the bus, the recorder or other participants until the
//   scheduler calls commit(). rollback() discards the trial and restores the
//   committed state and outputs (only required when canRollback).
// - A participant owns its state. The scheduler never changes it except
//   through these calls; the bus holds only committed outputs.
// - Inputs arrive with each call as an InputFrame: for continuous inputs a
//   value, optional derivatives and the time they were committed, so the
//   participant can evaluate the input anywhere inside its step.
// - Events a participant emits during doStep are trial events too: they are
//   discarded on rollback and scheduled on commit.

#include "sim_core/Signals.h"
#include "sim_core/Status.h"
#include "sim_core/Time.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sim
{
enum class TimingKind
{
    FixedStep,    // continuous dynamics, constant step `step`
    VariableStep, // continuous dynamics, any step up to `step`; may be shortened to land on events
    Sampled,      // discrete-time: acts every `step` (its outputs take effect after its sample, so it breaks direct-feedthrough loops)
};

struct Timing
{
    TimingKind kind = TimingKind::FixedStep;
    Tick step = 0;     // fixed or sample period; for VariableStep the largest step
    Tick minStep = 1;  // VariableStep: the shortest step it can take
};

struct Capabilities
{
    bool canRollback = false;
    bool providesNextEventTime = false; // nextEventTime() can end its next step early (VariableStep only)
    bool locatesEvents = false;         // doStep may stop early at a located event (needs VariableStep + canRollback)
    bool stateSerializable = false;     // saveState/restoreState: needed for checkpoints, reset and replay seek
    bool deterministic = true;
};

struct ParticipantInfo
{
    std::string name;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    Timing timing;
    Capabilities caps;
};

struct ParticipantConfig
{
    std::uint64_t seed = 0;                    // the participant's own random seed (recorded in the manifest)
    std::map<std::string, double> parameters;  // participant-defined
};

// A participant's inputs for one call.
class InputFrame
{
public:
    int size() const { return (int)slots.size(); }
    bool bound(int port) const { return slots[(std::size_t)port].bound; }
    // A continuous input at time `at`: held, or extrapolated from its last
    // committed value with the source's derivatives (Taylor, up to 2nd order).
    double real(int port, Tick at) const;
    // The committed value (discrete inputs; continuous inputs as held).
    Value value(int port) const { return slots[(std::size_t)port].value; }
    Tick committedAt(int port) const { return slots[(std::size_t)port].at; }

private:
    friend class Simulation;
    struct Slot
    {
        Value value;
        double d1 = 0.0, d2 = 0.0;
        Tick at = 0;
        bool bound = false;
        SignalKind kind = SignalKind::Continuous;
        InputPolicy policy = InputPolicy::Hold;
    };
    std::vector<Slot> slots;
};

// A participant's outputs, written by getOutputs().
class OutputFrame
{
public:
    void setReal(int port, double v, double d1 = 0.0, double d2 = 0.0)
    {
        auto& s = slots[(std::size_t)port];
        s.value = Value::ofReal(v);
        s.d1 = d1;
        s.d2 = d2;
    }
    void set(int port, Value v) { slots[(std::size_t)port].value = v; }
    int size() const { return (int)slots.size(); }

private:
    friend class Simulation;
    struct Slot
    {
        Value value;
        double d1 = 0.0, d2 = 0.0;
    };
    std::vector<Slot> slots;
};

enum class EventPriority : int { Fault = 0, External = 1, Participant = 2, Measurement = 3 };

struct Event
{
    SimTime time;
    int priority = (int)EventPriority::Participant;
    int source = -1;             // participant registration index; -1 = external
    std::uint64_t sequence = 0;  // insertion order, the final tie-break
    int signal = -1;             // bus signal id
    EventPayload payload;
    std::uint32_t flags = 0;     // EventFlags

    enum Flags : std::uint32_t
    {
        External = 1,   // an input from outside the model (test, UI, agent, hardware)
        ValueSet = 2,   // sets a continuous/discrete external signal's value (payload.value / payload.code)
        Scheduled = 4,  // external, at a stated time (drives the clock); otherwise stamped at a round boundary
    };
};

// Total order of events (architecture section 03): tick, microstep, priority
// class, source registration order, insertion sequence.
struct EventOrder
{
    bool operator()(const Event& a, const Event& b) const
    {
        if (a.time.tick != b.time.tick) return a.time.tick < b.time.tick;
        if (a.time.microstep != b.time.microstep) return a.time.microstep < b.time.microstep;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.source != b.source) return a.source < b.source;
        return a.sequence < b.sequence;
    }
};

// Where a participant emits events on its event-kind output ports.
class EventSink
{
public:
    virtual ~EventSink() = default;
    // `at` must be the current time or later (the end of the step, or for
    // handleEvent the event's own time, which then runs at the next microstep).
    virtual void emit(int outputPort, Tick at, EventPayload payload) = 0;
};

struct StepResult
{
    Status status;
    bool stoppedEarly = false; // located an event: the trial ends at stoppedAt
    Tick stoppedAt = 0;
    bool pauseRequested = false; // e.g. a debugger breakpoint: pause after this round
};

class Participant
{
public:
    virtual ~Participant() = default;

    virtual ParticipantInfo describe() const = 0;
    virtual Status configure(const ParticipantConfig&) { return {}; }
    // Initialisation pass `pass` at t0: compute outputs consistent with the
    // inputs (other participants' outputs from the previous pass).
    virtual Status initialize(Tick t0, int pass, const InputFrame& inputs) = 0;
    virtual Tick nextEventTime() const { return kNever; }
    virtual StepResult doStep(Tick from, Tick to, const InputFrame& inputs, EventSink& events) = 0;
    virtual void commit() {}
    virtual void rollback() {}
    virtual void getOutputs(OutputFrame& out) const = 0;
    // An event on input port `inputPort` at e.time. The participant may be
    // behind that time (a fixed-step participant between its steps). State it
    // changes here is committed immediately.
    virtual Status handleEvent(const Event& e, int inputPort, const InputFrame& inputs, EventSink& events)
    {
        (void)e; (void)inputPort; (void)inputs; (void)events;
        return {};
    }
    virtual std::vector<std::uint8_t> saveState() const { return {}; }
    virtual Status restoreState(const std::vector<std::uint8_t>&) { return Status::failure("This participant's state is not serializable."); }
    virtual void terminate() {}
};
}

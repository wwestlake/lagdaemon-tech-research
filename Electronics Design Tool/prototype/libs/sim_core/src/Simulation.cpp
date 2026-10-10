#include "sim_core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace sim
{
// ---- names ------------------------------------------------------------------

const char* toString(ValueType t)
{
    switch (t) { case ValueType::Real: return "real"; case ValueType::Integer: return "integer"; case ValueType::Boolean: return "boolean"; }
    return "?";
}
const char* toString(SignalKind k)
{
    switch (k) { case SignalKind::Continuous: return "continuous"; case SignalKind::Discrete: return "discrete"; case SignalKind::Event: return "event"; }
    return "?";
}
const char* toString(SignalRole r)
{
    switch (r) { case SignalRole::Normal: return "normal"; case SignalRole::Command: return "command"; case SignalRole::Measurement: return "measurement"; case SignalRole::Fault: return "fault"; }
    return "?";
}
const char* toString(Severity s)
{
    switch (s) { case Severity::Info: return "info"; case Severity::Warning: return "warning"; case Severity::Error: return "error"; }
    return "?";
}
const char* toString(Category c)
{
    switch (c)
    {
        case Category::Configuration: return "configuration";
        case Category::Lifecycle: return "lifecycle";
        case Category::Convergence: return "convergence";
        case Category::Numerical: return "numerical";
        case Category::Timing: return "timing";
        case Category::Synchronisation: return "synchronisation";
        case Category::Events: return "events";
    }
    return "?";
}
const char* toString(LifecycleState s)
{
    switch (s)
    {
        case LifecycleState::Created: return "Created";
        case LifecycleState::Configured: return "Configured";
        case LifecycleState::Initialized: return "Initialized";
        case LifecycleState::Running: return "Running";
        case LifecycleState::Paused: return "Paused";
        case LifecycleState::Faulted: return "Faulted";
        case LifecycleState::Terminated: return "Terminated";
    }
    return "?";
}

// ---- frames -----------------------------------------------------------------

double InputFrame::real(int port, Tick at) const
{
    const auto& s = slots[(std::size_t)port];
    if (s.kind != SignalKind::Continuous || s.policy == InputPolicy::Hold || !s.bound)
        return s.value.asReal();
    const double dt = ticksToSeconds(at - s.at);
    return s.value.real + s.d1 * dt + 0.5 * s.d2 * dt * dt;
}

class Simulation::Sink final : public EventSink
{
public:
    void emit(int outputPort, Tick at, EventPayload payload) override { emissions.push_back({ outputPort, at, payload }); }
    std::vector<PendingEmission> emissions;
};

// ---- construction -----------------------------------------------------------

Simulation::Simulation() = default;
Simulation::~Simulation() = default;

Status Simulation::refuseUnless(bool allowed, const char* action)
{
    if (allowed)
        return {};
    lastError_ = std::string("Cannot ") + action + " while the simulation is " + toString(state_) + ".";
    return Status::failure(lastError_);
}

void Simulation::note(Severity severity, Category category, const std::string& participant, const std::string& message)
{
    Diagnostic d;
    d.time = clock_;
    d.severity = severity;
    d.category = category;
    d.participant = participant;
    d.message = message;
    if (recordingStarted_)
        current_.diagnostics.push_back(d);
    diagnostics_.add(std::move(d));
}

Status Simulation::fail(Category category, const std::string& participant, const std::string& message)
{
    lastError_ = participant.empty() ? message : participant + ": " + message;
    note(Severity::Error, category, participant, message);
    return Status::failure(lastError_);
}

int Simulation::findParticipant(const std::string& name) const
{
    for (int i = 0; i < (int)participants_.size(); ++i)
        if (participants_[(std::size_t)i].info.name == name)
            return i;
    return -1;
}

int Simulation::signalId(const std::string& name) const
{
    for (const auto& s : signals_)
        if (s.name == name)
            return s.id;
    return -1;
}

Value Simulation::committedValue(int signal) const
{
    return signal >= 0 && signal < (int)committed_.size() ? committed_[(std::size_t)signal].value : Value {};
}

const ParticipantInfo& Simulation::participantInfo(int index) const { return participants_[(std::size_t)index].info; }
Participant& Simulation::participant(int index) { return *participants_[(std::size_t)index].participant; }

LifecycleState Simulation::participantState(int index) const
{
    if (index < 0 || index >= (int)participants_.size())
        return state_;
    return participants_[(std::size_t)index].faulted ? LifecycleState::Faulted : state_;
}

int Simulation::addParticipant(std::unique_ptr<Participant> participant, ParticipantConfig config)
{
    if (!refuseUnless(state_ == LifecycleState::Created, "add a participant"))
        return -1;
    if (participant == nullptr)
    {
        lastError_ = "No participant given.";
        return -1;
    }
    auto info = participant->describe();
    auto reject = [this](const std::string& why) { lastError_ = why; return -1; };
    if (info.name.empty() || info.name.find('.') != std::string::npos)
        return reject("Participant names must be non-empty and contain no dots (\"" + info.name + "\").");
    if (findParticipant(info.name) >= 0)
        return reject("A participant named " + info.name + " already exists.");
    auto unique = [](const std::vector<PortSpec>& ports, std::string& duplicate) {
        for (std::size_t a = 0; a < ports.size(); ++a)
            for (std::size_t b = a + 1; b < ports.size(); ++b)
                if (ports[a].name == ports[b].name) { duplicate = ports[a].name; return false; }
        return true;
    };
    std::string duplicate;
    if (!unique(info.inputs, duplicate) || !unique(info.outputs, duplicate))
        return reject(info.name + " declares port " + duplicate + " twice.");
    std::vector<Unit> outputUnits;
    for (const auto& port : info.outputs)
    {
        Unit unit;
        std::string error;
        if (port.kind != SignalKind::Event && !parseUnit(port.unit, unit, error))
            return reject(info.name + "." + port.name + ": " + error);
        if (signalId(info.name + "." + port.name) >= 0)
            return reject("A signal named " + info.name + "." + port.name + " already exists.");
        outputUnits.push_back(unit);
    }
    for (const auto& port : info.inputs)
    {
        Unit unit;
        std::string error;
        if (port.kind != SignalKind::Event && !parseUnit(port.unit, unit, error))
            return reject(info.name + "." + port.name + ": " + error);
    }

    const int index = (int)participants_.size();
    Entry entry;
    entry.info = info;
    entry.config = config;
    entry.inputs.resize(info.inputs.size());
    entry.trialOutputs.slots.resize(info.outputs.size());
    for (std::size_t k = 0; k < info.outputs.size(); ++k)
    {
        SignalInfo s;
        s.id = (int)signals_.size();
        s.name = info.name + "." + info.outputs[k].name;
        s.spec = info.outputs[k];
        s.unit = outputUnits[k];
        s.writer = index;
        Committed c;
        c.value.type = s.spec.type;
        signals_.push_back(s);
        committed_.push_back(c);
        subscribers_.emplace_back();
        entry.outputs.push_back(s.id);
    }
    entry.participant = std::move(participant);
    participants_.push_back(std::move(entry));
    return index;
}

Status Simulation::addExternalSignal(const std::string& name, PortSpec spec)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Created, "add an external signal"); !s)
        return s;
    if (name.empty() || signalId(name) >= 0)
        return Status::failure(lastError_ = "External signal names must be unique and non-empty (\"" + name + "\").");
    Unit unit;
    std::string error;
    if (spec.kind != SignalKind::Event && !parseUnit(spec.unit, unit, error))
        return Status::failure(lastError_ = name + ": " + error);
    SignalInfo s;
    s.id = (int)signals_.size();
    s.name = name;
    s.spec = spec;
    s.unit = unit;
    s.writer = -1;
    Committed c;
    c.value = spec.defaultValue;
    c.value.type = spec.type;
    signals_.push_back(s);
    committed_.push_back(c);
    subscribers_.emplace_back();
    return {};
}

Status Simulation::connect(const std::string& output, const std::string& input, BindOptions options)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Created, "connect signals"); !s)
        return s;
    auto failure = [this](const std::string& why) { lastError_ = why; return Status::failure(why); };
    const int signal = signalId(output);
    if (signal < 0)
        return failure("No signal named " + output + ".");
    const auto dot = input.rfind('.');
    if (dot == std::string::npos)
        return failure("Inputs are named participant.port (\"" + input + "\").");
    const int p = findParticipant(input.substr(0, dot));
    if (p < 0)
        return failure("No participant named " + input.substr(0, dot) + ".");
    auto& entry = participants_[(std::size_t)p];
    int port = -1;
    for (int k = 0; k < (int)entry.info.inputs.size(); ++k)
        if (entry.info.inputs[(std::size_t)k].name == input.substr(dot + 1))
            port = k;
    if (port < 0)
        return failure(entry.info.name + " has no input named " + input.substr(dot + 1) + ".");
    auto& binding = entry.inputs[(std::size_t)port];
    if (binding.signal >= 0)
        return failure(input + " is already connected to " + signals_[(std::size_t)binding.signal].name + "; an input has exactly one source.");
    const auto& source = signals_[(std::size_t)signal];
    const auto& spec = entry.info.inputs[(std::size_t)port];
    if (source.spec.kind != spec.kind)
        return failure("Cannot connect " + output + " (" + toString(source.spec.kind) + ") to " + input + " (" + toString(spec.kind) + ").");
    Conversion conversion;
    if (spec.kind != SignalKind::Event)
    {
        if (source.spec.type != spec.type)
            return failure("Cannot connect " + output + " (" + toString(source.spec.type) + ") to " + input + " (" + toString(spec.type) + ").");
        Unit target;
        std::string error;
        parseUnit(spec.unit, target, error);
        if (!conversionBetween(source.unit, target, conversion))
            return failure("Cannot connect " + output + " [" + source.unit.symbol + ", " + describeDimension(source.unit.dimension) + "] to " + input
                           + " [" + target.symbol + ", " + describeDimension(target.dimension) + "]: the dimensions differ.");
        if (!conversion.identity() && !options.convertUnits)
            return failure("Connecting " + output + " [" + source.unit.symbol + "] to " + input + " [" + target.symbol
                           + "] needs an explicit unit conversion; connect it with conversion enabled.");
        if (spec.type != ValueType::Real && !conversion.identity())
            return failure("Unit conversion applies to real signals only (" + output + " -> " + input + ").");
    }
    binding.signal = signal;
    binding.conversion = conversion;
    if (spec.kind == SignalKind::Event)
    {
        subscribers_[(std::size_t)signal].push_back({ p, port });
        std::sort(subscribers_[(std::size_t)signal].begin(), subscribers_[(std::size_t)signal].end());
    }
    return {};
}

// ---- configuration ----------------------------------------------------------

Status Simulation::validateTopology()
{
    for (const auto& e : participants_)
    {
        const auto& name = e.info.name;
        const auto& t = e.info.timing;
        const auto& c = e.info.caps;
        if (t.step <= 0)
            return fail(Category::Configuration, name, "its step must be greater than zero.");
        if (t.kind == TimingKind::VariableStep && (t.minStep < 1 || t.minStep > t.step))
            return fail(Category::Configuration, name, "its minimum step must be between 1 tick and its largest step.");
        if (c.locatesEvents && (!c.canRollback || t.kind != TimingKind::VariableStep))
            return fail(Category::Configuration, name, "an event-locating participant must be variable-step and able to roll back.");
        if (c.providesNextEventTime && t.kind != TimingKind::VariableStep)
            return fail(Category::Configuration, name, "only a variable-step participant can end its step at its own next event time.");
        for (std::size_t k = 0; k < e.info.inputs.size(); ++k)
            if (e.info.inputs[k].required && e.inputs[k].signal < 0)
                return fail(Category::Configuration, name, "input " + name + "." + e.info.inputs[k].name + " is required but not connected.");
        for (const auto& out : e.info.outputs)
            for (const auto& dep : out.dependsOn)
                if (std::none_of(e.info.inputs.begin(), e.info.inputs.end(), [&](const PortSpec& in) { return in.name == dep; }))
                    return fail(Category::Configuration, name, "output " + out.name + " depends on " + dep + ", which is not one of its inputs.");
    }
    return {};
}

// Conflict C2: a loop of direct-feedthrough connections through continuous
// participants would be an algebraic loop across participants; it is refused.
// A sampled participant breaks the loop (its output takes effect after its
// sample). A loop inside one participant is that participant's business.
Status Simulation::checkFeedthroughLoops()
{
    const int n = (int)signals_.size();
    std::vector<std::vector<int>> next((std::size_t)n);
    for (int q = 0; q < (int)participants_.size(); ++q)
    {
        const auto& e = participants_[(std::size_t)q];
        if (e.info.timing.kind == TimingKind::Sampled)
            continue;
        for (std::size_t o = 0; o < e.info.outputs.size(); ++o)
            for (const auto& dep : e.info.outputs[o].dependsOn)
                for (std::size_t k = 0; k < e.info.inputs.size(); ++k)
                    if (e.info.inputs[k].name == dep && e.inputs[k].signal >= 0)
                        next[(std::size_t)e.inputs[k].signal].push_back(e.outputs[o]);
    }
    std::vector<int> mark((std::size_t)n, 0), stack;
    std::vector<int> cycle;
    std::function<bool(int)> visit = [&](int s) {
        mark[(std::size_t)s] = 1;
        stack.push_back(s);
        for (int t : next[(std::size_t)s])
        {
            if (mark[(std::size_t)t] == 1)
            {
                cycle.assign(std::find(stack.begin(), stack.end(), t), stack.end());
                return true;
            }
            if (mark[(std::size_t)t] == 0 && visit(t))
                return true;
        }
        stack.pop_back();
        mark[(std::size_t)s] = 2;
        return false;
    };
    for (int s = 0; s < n; ++s)
    {
        if (mark[(std::size_t)s] != 0 || !visit(s))
            continue;
        const int writer = signals_[(std::size_t)cycle.front()].writer;
        const bool onePart = std::all_of(cycle.begin(), cycle.end(), [&](int c) { return signals_[(std::size_t)c].writer == writer; });
        if (onePart)
        {
            std::fill(mark.begin(), mark.end(), 0);
            for (int c : cycle) mark[(std::size_t)c] = 2;
            stack.clear();
            continue;
        }
        std::string path;
        for (int c : cycle) path += signals_[(std::size_t)c].name + " -> ";
        path += signals_[(std::size_t)cycle.front()].name;
        return fail(Category::Configuration, {}, "algebraic loop across participants with no delay: " + path
                    + ". Put both sides in one participant, or make one of them sampled.");
    }
    return {};
}

Status Simulation::configure()
{
    if (auto s = refuseUnless(state_ == LifecycleState::Created, "configure"); !s)
        return s;
    if (auto s = validateTopology(); !s)
        return s;
    if (auto s = checkFeedthroughLoops(); !s)
        return s;
    for (auto& e : participants_)
        if (auto s = e.participant->configure(e.config); !s)
            return fail(Category::Configuration, e.info.name, s.message);
    state_ = LifecycleState::Configured;
    return {};
}

// ---- inputs and outputs -----------------------------------------------------

void Simulation::buildFrame(int index, Tick at, InputFrame& frame)
{
    auto& e = participants_[(std::size_t)index];
    frame.slots.resize(e.info.inputs.size());
    for (std::size_t k = 0; k < e.info.inputs.size(); ++k)
    {
        const auto& spec = e.info.inputs[k];
        auto& slot = frame.slots[k];
        slot.kind = spec.kind;
        slot.policy = spec.policy;
        const auto& b = e.inputs[k];
        if (b.signal < 0)
        {
            slot = {};
            slot.kind = spec.kind;
            slot.value = spec.defaultValue;
            slot.value.type = spec.type;
            continue;
        }
        const auto& c = committed_[(std::size_t)b.signal];
        slot.bound = true;
        slot.value = c.value;
        slot.at = c.at;
        slot.d1 = b.conversion.derivative(c.d1);
        slot.d2 = b.conversion.derivative(c.d2);
        if (slot.value.type == ValueType::Real)
            slot.value.real = b.conversion.value(c.value.real);
        if (spec.kind == SignalKind::Continuous && spec.policy == InputPolicy::Extrapolate && spec.maxExtrapolation > 0
            && at - c.at > spec.maxExtrapolation)
            note(Severity::Warning, Category::Synchronisation, e.info.name,
                 "input " + spec.name + " read " + signals_[(std::size_t)b.signal].name + " " + describeTick(at - c.at)
                     + " ahead of its last value (limit " + describeTick(spec.maxExtrapolation) + ").");
    }
}

Status Simulation::readOutputs(int index, bool)
{
    auto& e = participants_[(std::size_t)index];
    e.participant->getOutputs(e.trialOutputs);
    for (std::size_t k = 0; k < e.info.outputs.size(); ++k)
    {
        const auto& spec = e.info.outputs[k];
        if (spec.kind == SignalKind::Event)
            continue;
        const auto& slot = e.trialOutputs.slots[k];
        if (slot.value.type != spec.type)
            return Status::failure("output " + spec.name + " was given a " + toString(slot.value.type) + " value; it is " + toString(spec.type) + ".");
        if (spec.type == ValueType::Real && (!std::isfinite(slot.value.real) || !std::isfinite(slot.d1) || !std::isfinite(slot.d2)))
            return Status::failure("output " + spec.name + " is not finite (NaN or infinity).");
    }
    return {};
}

void Simulation::publishOutputs(int index, Tick continuousAt, Tick discreteAt, std::uint32_t microstep, bool onlyChanged)
{
    auto& e = participants_[(std::size_t)index];
    for (std::size_t k = 0; k < e.info.outputs.size(); ++k)
    {
        const auto& spec = e.info.outputs[k];
        if (spec.kind == SignalKind::Event)
            continue;
        const auto& slot = e.trialOutputs.slots[k];
        auto& c = committed_[(std::size_t)e.outputs[k]];
        Committed next;
        next.value = slot.value;
        next.d1 = spec.derivatives >= 1 ? slot.d1 : 0.0;
        next.d2 = spec.derivatives >= 2 ? slot.d2 : 0.0;
        next.at = spec.kind == SignalKind::Continuous ? continuousAt : discreteAt;
        const bool changed = !next.value.identical(c.value) || std::memcmp(&next.d1, &c.d1, sizeof(double)) != 0
                             || std::memcmp(&next.d2, &c.d2, sizeof(double)) != 0;
        const bool record = spec.kind == SignalKind::Continuous ? (!onlyChanged || changed) : changed;
        c = next;
        // Recorded at the time the value changed (an event's time when a
        // handler changed it); the commit time stays the extrapolation anchor.
        if (record && recordingStarted_)
            current_.samples.push_back({ e.outputs[k], { discreteAt, microstep }, next.value });
    }
}

Tick Simulation::dueTime(int index) const
{
    const auto& e = participants_[(std::size_t)index];
    Tick due = kNever;
    if (!checkedAdd(e.lastCommit, e.info.timing.step, due))
        return kNever;
    if (e.info.timing.kind == TimingKind::VariableStep && e.info.caps.providesNextEventTime)
    {
        const auto next = e.participant->nextEventTime();
        if (next != kNever && next > e.lastCommit && next < due)
            due = next;
    }
    return due;
}

Tick Simulation::peekNextRoundTime() const
{
    Tick t = kNever;
    for (int i = 0; i < (int)participants_.size(); ++i)
        t = std::min(t, dueTime(i));
    if (!queue_.empty())
        t = std::min(t, queue_.begin()->time.tick);
    return t;
}

// ---- lifecycle --------------------------------------------------------------

Status Simulation::initialize(Tick t0)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Configured, "initialise"); !s)
        return s;
    if (t0 < 0 || t0 > kLastSchedulable)
        return fail(Category::Lifecycle, {}, "the start time must be between 0 and " + describeTick(kLastSchedulable) + ".");
    clock_ = { t0, 0 };
    for (auto& e : participants_)
        e.lastCommit = t0;
    for (auto& c : committed_)
        c.at = t0;

    InputFrame frame;
    std::vector<std::string> changing;
    bool converged = false;
    for (int pass = 0; pass < initPassLimit_ && !converged; ++pass)
    {
        changing.clear();
        for (int i = 0; i < (int)participants_.size(); ++i)
        {
            auto& e = participants_[(std::size_t)i];
            buildFrame(i, t0, frame);
            if (auto s = e.participant->initialize(t0, pass, frame); !s)
                return fail(Category::Convergence, e.info.name, "initialisation failed: " + s.message);
            if (auto s = readOutputs(i, true); !s)
                return fail(Category::Numerical, e.info.name, s.message);
            bool changed = false;
            for (std::size_t k = 0; k < e.info.outputs.size(); ++k)
            {
                const auto& c = committed_[(std::size_t)e.outputs[k]];
                const auto& slot = e.trialOutputs.slots[k];
                if (e.info.outputs[k].kind != SignalKind::Event
                    && (!slot.value.identical(c.value) || std::memcmp(&slot.d1, &c.d1, sizeof(double)) != 0 || std::memcmp(&slot.d2, &c.d2, sizeof(double)) != 0))
                    changed = true;
            }
            publishOutputs(i, t0, t0, 0, false);
            if (changed)
                changing.push_back(e.info.name);
        }
        converged = pass >= 1 && changing.empty();
    }
    if (!converged)
    {
        std::string names;
        for (const auto& n : changing) names += (names.empty() ? "" : ", ") + n;
        return fail(Category::Convergence, {}, "initialisation did not converge after " + std::to_string(initPassLimit_) + " passes; still changing: " + names + ".");
    }
    for (int i = 0; i < (int)participants_.size(); ++i)
        if (dueTime(i) == kNever)
            return fail(Category::Lifecycle, participants_[(std::size_t)i].info.name,
                        "its first step would end beyond the clock's range (" + describeTick(kLastSchedulable) + ").");

    t0_ = t0;
    roundCount_ = 0;
    state_ = LifecycleState::Initialized;
    Status cp;
    t0Checkpoint_ = checkpoint(cp); // null when some participant cannot save its state (reset then refuses)
    return {};
}

void Simulation::beginRecording()
{
    if (recorder_ == nullptr || recordingStarted_)
        return;
    RunDescription run;
    run.signals = signals_;
    run.t0 = t0_;
    for (int i = 0; i < (int)participants_.size(); ++i)
    {
        const auto& e = participants_[(std::size_t)i];
        run.participants.push_back({ e.info.name, i, e.info.timing, e.info.caps, e.config.seed });
    }
    recorder_->begin(run);
    recordingStarted_ = true;
    RoundRecord initial;
    initial.round = roundCount_;
    initial.time = clock_;
    for (std::size_t s = 0; s < signals_.size(); ++s)
        if (signals_[s].spec.kind != SignalKind::Event)
            initial.samples.push_back({ (int)s, { committed_[s].at, 0 }, committed_[s].value });
    recorder_->round(std::move(initial));
}

Status Simulation::start()
{
    if (auto s = refuseUnless(state_ == LifecycleState::Initialized || state_ == LifecycleState::Paused, "start"); !s)
        return s;
    beginRecording();
    state_ = LifecycleState::Running;
    return {};
}

Status Simulation::pause()
{
    if (auto s = refuseUnless(state_ == LifecycleState::Running, "pause"); !s)
        return s;
    state_ = LifecycleState::Paused;
    return {};
}

Status Simulation::terminate()
{
    if (auto s = refuseUnless(state_ != LifecycleState::Terminated, "terminate"); !s)
        return s;
    for (auto& e : participants_)
        e.participant->terminate();
    if (recordingStarted_ && recorder_ != nullptr)
        recorder_->end();
    recordingStarted_ = false;
    state_ = LifecycleState::Terminated;
    return {};
}

Status Simulation::reset()
{
    if (auto s = refuseUnless(state_ == LifecycleState::Paused || state_ == LifecycleState::Faulted || state_ == LifecycleState::Initialized, "reset"); !s)
        return s;
    if (t0Checkpoint_ == nullptr)
    {
        std::string names;
        for (const auto& e : participants_)
            if (!e.info.caps.stateSerializable) names += (names.empty() ? "" : ", ") + e.info.name;
        return fail(Category::Lifecycle, {}, "reset needs every participant to save its state; these cannot: " + names + ".");
    }
    if (auto s = restore(*t0Checkpoint_); !s)
        return s;
    for (auto& e : participants_)
        e.faulted = false;
    if (recordingStarted_ && recorder_ != nullptr)
        recorder_->end();
    recordingStarted_ = false;
    recorder_ = nullptr; // a new run takes a new recorder
    state_ = LifecycleState::Initialized;
    return {};
}

std::shared_ptr<const Simulation::Checkpoint> Simulation::checkpoint(Status& status) const
{
    auto cp = std::make_shared<Checkpoint>();
    for (const auto& e : participants_)
    {
        if (!e.info.caps.stateSerializable)
        {
            status = Status::failure(e.info.name + " cannot save its state.");
            return nullptr;
        }
        cp->participants.push_back({ e.participant->saveState(), e.lastCommit });
    }
    cp->clock = clock_;
    cp->rounds = roundCount_;
    cp->sequence = sequence_;
    cp->committed = committed_;
    cp->queue.assign(queue_.begin(), queue_.end());
    cp->pendingBoundary = pendingBoundary_;
    cp->recordedBoundary = recordedBoundary_;
    status = {};
    return cp;
}

Status Simulation::restore(const Checkpoint& cp)
{
    if (cp.participants.size() != participants_.size())
        return fail(Category::Lifecycle, {}, "the checkpoint is from a different simulation.");
    for (std::size_t i = 0; i < participants_.size(); ++i)
    {
        auto& e = participants_[i];
        if (auto s = e.participant->restoreState(cp.participants[i].blob); !s)
            return fail(Category::Lifecycle, e.info.name, "could not restore its state: " + s.message);
        e.lastCommit = cp.participants[i].lastCommit;
        e.trialEvents.clear();
    }
    clock_ = cp.clock;
    roundCount_ = cp.rounds;
    sequence_ = cp.sequence;
    committed_ = cp.committed;
    queue_.clear();
    queue_.insert(cp.queue.begin(), cp.queue.end());
    pendingBoundary_ = cp.pendingBoundary;
    recordedBoundary_ = cp.recordedBoundary;
    return {};
}

// ---- events -----------------------------------------------------------------

Status Simulation::pushEvent(Event e)
{
    if (e.time.tick > kLastSchedulable)
        return Status::failure("an event at " + describeTick(e.time.tick) + " is beyond the clock's range.");
    if (e.time < clock_)
        return Status::failure("an event at " + describeTick(e.time.tick) + " (microstep " + std::to_string(e.time.microstep)
                               + ") is in the past; the time is " + describeTick(clock_.tick) + ".");
    e.sequence = sequence_++;
    queue_.insert(e);
    return {};
}

Status Simulation::setExternal(const std::string& name, Value value)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Initialized || state_ == LifecycleState::Running || state_ == LifecycleState::Paused, "set an external input"); !s)
        return s;
    const int id = signalId(name);
    if (id < 0 || signals_[(std::size_t)id].writer != -1 || signals_[(std::size_t)id].spec.kind == SignalKind::Event)
        return Status::failure(lastError_ = name + " is not an external continuous or discrete signal.");
    if (value.type != signals_[(std::size_t)id].spec.type)
        return Status::failure(lastError_ = name + " is " + toString(signals_[(std::size_t)id].spec.type) + ", not " + toString(value.type) + ".");
    if (value.type == ValueType::Real && !std::isfinite(value.real))
        return Status::failure(lastError_ = name + ": the value is not finite.");
    Event e;
    e.priority = (int)EventPriority::External;
    e.signal = id;
    e.flags = Event::External | Event::ValueSet;
    e.payload.value = value.real;
    e.payload.code = value.integer;
    pendingBoundary_.push_back(e);
    return {};
}

Status Simulation::emitExternal(const std::string& name, EventPayload payload)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Initialized || state_ == LifecycleState::Running || state_ == LifecycleState::Paused, "emit an external event"); !s)
        return s;
    const int id = signalId(name);
    if (id < 0 || signals_[(std::size_t)id].writer != -1 || signals_[(std::size_t)id].spec.kind != SignalKind::Event)
        return Status::failure(lastError_ = name + " is not an external event signal.");
    Event e;
    e.priority = (int)EventPriority::External;
    e.signal = id;
    e.flags = Event::External;
    e.payload = payload;
    pendingBoundary_.push_back(e);
    return {};
}

Status Simulation::scheduleExternal(const std::string& name, Tick at, EventPayload payload)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Initialized || state_ == LifecycleState::Running || state_ == LifecycleState::Paused, "schedule an external input"); !s)
        return s;
    const int id = signalId(name);
    if (id < 0 || signals_[(std::size_t)id].writer != -1)
        return Status::failure(lastError_ = name + " is not an external signal.");
    if (at <= clock_.tick)
        return fail(Category::Events, {}, "a scheduled input on " + name + " at " + describeTick(at) + " is not in the future (now " + describeTick(clock_.tick) + ").");
    Event e;
    e.time = { at, 0 };
    e.priority = (int)EventPriority::External;
    e.signal = id;
    e.flags = Event::External | Event::Scheduled | (signals_[(std::size_t)id].spec.kind == SignalKind::Event ? 0u : (std::uint32_t)Event::ValueSet);
    e.payload = payload;
    if (auto s = pushEvent(e); !s)
        return fail(Category::Events, {}, s.message);
    return {};
}

Status Simulation::queueRecordedExternal(const Event& recorded)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Initialized || state_ == LifecycleState::Paused, "queue a recorded input"); !s)
        return s;
    if (recorded.signal < 0 || recorded.signal >= (int)signals_.size() || signals_[(std::size_t)recorded.signal].writer != -1)
        return Status::failure(lastError_ = "The recorded input names a signal that is not external in this simulation.");
    Event e = recorded;
    if (e.flags & Event::Scheduled)
    {
        e.time.microstep = 0;
        return pushEvent(e);
    }
    recordedBoundary_.push_back(e);
    return {};
}

RoundOutcome Simulation::faultRound(const std::string& participant, Category category, const std::string& message)
{
    const int index = findParticipant(participant);
    if (index >= 0)
        participants_[(std::size_t)index].faulted = true;
    state_ = LifecycleState::Faulted;
    fail(category, participant, message);
    if (recordingStarted_ && recorder_ != nullptr)
    {
        current_.round = roundCount_;
        current_.time = clock_;
        recorder_->round(std::move(current_));
    }
    current_ = {};
    RoundOutcome out;
    out.kind = RoundOutcome::Kind::Faulted;
    out.time = clock_;
    return out;
}

Status Simulation::processEvents(Tick t, int& processed)
{
    InputFrame frame;
    while (!queue_.empty() && queue_.begin()->time.tick == t)
    {
        const Event e = *queue_.begin();
        if ((int)e.time.microstep > cascadeLimit_)
        {
            std::string chain;
            int shown = 0;
            for (auto it = current_.events.rbegin(); it != current_.events.rend() && shown < 6; ++it, ++shown)
                chain += (shown ? " <- " : "") + signals_[(std::size_t)it->signal].name + "@" + std::to_string(it->time.microstep);
            queue_.clear();
            return Status::failure("event cascade does not settle at " + describeTick(t) + ": more than " + std::to_string(cascadeLimit_)
                                   + " microsteps (chattering or an event loop). Last events: " + chain + ".");
        }
        queue_.erase(queue_.begin());
        clock_.microstep = e.time.microstep;
        current_.events.push_back(e);
        ++processed;
        const auto& signal = signals_[(std::size_t)e.signal];
        if (e.flags & Event::ValueSet)
        {
            auto& c = committed_[(std::size_t)e.signal];
            Value v;
            v.type = signal.spec.type;
            v.real = e.payload.value;
            v.integer = e.payload.code;
            c.value = v;
            c.d1 = c.d2 = 0.0;
            c.at = t;
            if (recordingStarted_)
                current_.samples.push_back({ e.signal, e.time, v });
            continue;
        }
        for (const auto& [p, port] : subscribers_[(std::size_t)e.signal])
        {
            auto& entry = participants_[(std::size_t)p];
            // A variable-step subscriber still behind this time (an event a
            // handler emitted in a cascade) is first stepped to it and
            // committed; every input it reads is committed at this time.
            if (entry.info.timing.kind == TimingKind::VariableStep && entry.lastCommit < t)
            {
                buildFrame(p, t, frame);
                Sink stepSink;
                const auto r = entry.participant->doStep(entry.lastCommit, t, frame, stepSink);
                if (!r.status)
                {
                    entry.faulted = true;
                    return Status::failure(entry.info.name + ": step to " + describeTick(t) + " (to receive " + signal.name + ") failed: " + r.status.message);
                }
                if (r.stoppedEarly && r.stoppedAt < t)
                {
                    entry.faulted = true;
                    return Status::failure(entry.info.name + ": located its own event at " + describeTick(r.stoppedAt) + " while being brought to "
                                           + describeTick(t) + " to receive " + signal.name + "; that time has already been committed.");
                }
                if (auto s = readOutputs(p, true); !s)
                {
                    entry.faulted = true;
                    return Status::failure(entry.info.name + ": " + s.message);
                }
                entry.participant->commit();
                entry.lastCommit = t;
                publishOutputs(p, t, t, e.time.microstep, false);
                for (const auto& em : stepSink.emissions)
                {
                    if (em.port < 0 || em.port >= (int)entry.info.outputs.size() || entry.info.outputs[(std::size_t)em.port].kind != SignalKind::Event || em.at < t)
                        return Status::failure(entry.info.name + ": emitted an invalid or past event while being brought to " + describeTick(t) + ".");
                    Event out;
                    out.time = { em.at, em.at == t ? e.time.microstep + 1 : 0u };
                    out.priority = (int)EventPriority::Participant;
                    out.source = p;
                    out.signal = entry.outputs[(std::size_t)em.port];
                    out.payload = em.payload;
                    if (auto s = pushEvent(out); !s)
                        return Status::failure(entry.info.name + ": " + s.message);
                }
            }
            buildFrame(p, t, frame);
            Sink sink;
            if (auto s = entry.participant->handleEvent(e, port, frame, sink); !s)
            {
                entry.faulted = true;
                return Status::failure(entry.info.name + ": handling " + signal.name + " failed: " + s.message);
            }
            for (const auto& em : sink.emissions)
            {
                if (em.port < 0 || em.port >= (int)entry.info.outputs.size() || entry.info.outputs[(std::size_t)em.port].kind != SignalKind::Event)
                    return Status::failure(entry.info.name + ": emitted on port " + std::to_string(em.port) + ", which is not one of its event outputs.");
                if (em.at < t)
                    return Status::failure(entry.info.name + ": emitted an event at " + describeTick(em.at) + ", in the past (now " + describeTick(t) + ").");
                Event out;
                out.time = { em.at, em.at == t ? e.time.microstep + 1 : 0u };
                out.priority = (int)EventPriority::Participant;
                out.source = p;
                out.signal = entry.outputs[(std::size_t)em.port];
                out.payload = em.payload;
                if (auto s = pushEvent(out); !s)
                    return Status::failure(entry.info.name + ": " + s.message);
            }
            if (auto s = readOutputs(p, false); !s)
            {
                entry.faulted = true;
                return Status::failure(entry.info.name + ": " + s.message);
            }
            publishOutputs(p, entry.lastCommit, t, e.time.microstep, true);
        }
    }
    clock_.microstep = 0;
    return {};
}

// ---- the round --------------------------------------------------------------

RoundOutcome Simulation::advanceRound()
{
    RoundOutcome outcome;
    if (state_ != LifecycleState::Running)
    {
        lastError_ = std::string("Cannot advance while the simulation is ") + toString(state_) + ".";
        return outcome;
    }
    current_ = {};
    const int n = (int)participants_.size();

    // 1. The round's end.
    std::vector<Tick> due((std::size_t)n, kNever);
    Tick tNext = kNever;
    for (int i = 0; i < n; ++i)
    {
        auto& e = participants_[(std::size_t)i];
        Tick d = kNever;
        if (!checkedAdd(e.lastCommit, e.info.timing.step, d))
            return faultRound(e.info.name, Category::Lifecycle, "its next step would end beyond the clock's range (" + describeTick(kLastSchedulable) + ").");
        if (e.info.timing.kind == TimingKind::VariableStep && e.info.caps.providesNextEventTime)
        {
            const auto next = e.participant->nextEventTime();
            if (next != kNever && next <= e.lastCommit)
                return faultRound(e.info.name, Category::Events, "its next event time " + describeTick(next) + " is not after its last step (" + describeTick(e.lastCommit) + ").");
            if (next < d)
                d = next;
        }
        due[(std::size_t)i] = d;
        tNext = std::min(tNext, d);
    }
    if (!queue_.empty())
        tNext = std::min(tNext, queue_.begin()->time.tick);
    if (tNext == kNever)
    {
        outcome.kind = RoundOutcome::Kind::Idle;
        outcome.time = clock_;
        return outcome;
    }
    if (tNext <= clock_.tick)
        return faultRound({}, Category::Events, "the scheduler found work at " + describeTick(tNext) + ", not after the current time " + describeTick(clock_.tick) + ".");

    // Variable-step participants are brought to the round's end when they
    // were due at its first end, or when an event they subscribe to may
    // arrive at it: one is queued for that time, an external input will ride
    // on that boundary, or a writer of one of their event inputs steps in
    // this round (its events are only known after it steps).
    std::vector<char> firstDue((std::size_t)n, 0);
    for (int i = 0; i < n; ++i)
        firstDue[(std::size_t)i] = due[(std::size_t)i] == tNext;
    auto externalAt = [&](int signal, Tick t) {
        for (const auto& ev : pendingBoundary_)
            if (ev.signal == signal) return true;
        for (const auto& ev : recordedBoundary_)
            if (ev.time.tick == t && ev.signal == signal) return true;
        return false;
    };
    auto dueAt = [&](Tick t, const std::vector<int>&) {
        std::vector<char> in((std::size_t)n, 0);
        for (int i = 0; i < n; ++i)
        {
            const bool variable = participants_[(std::size_t)i].info.timing.kind == TimingKind::VariableStep;
            in[(std::size_t)i] = due[(std::size_t)i] == t || (variable && firstDue[(std::size_t)i]);
        }
        for (const auto& ev : queue_)
        {
            if (ev.time.tick > t) break;
            if (ev.time.tick == t)
                for (const auto& sub : subscribers_[(std::size_t)ev.signal])
                    if (participants_[(std::size_t)sub.first].info.timing.kind == TimingKind::VariableStep)
                        in[(std::size_t)sub.first] = 1;
        }
        for (bool grew = true; grew;)
        {
            grew = false;
            for (int i = 0; i < n; ++i)
            {
                const auto& e = participants_[(std::size_t)i];
                if (in[(std::size_t)i] || e.info.timing.kind != TimingKind::VariableStep)
                    continue;
                for (std::size_t k = 0; k < e.inputs.size() && !in[(std::size_t)i]; ++k)
                {
                    const int s = e.inputs[k].signal;
                    if (s < 0 || e.info.inputs[k].kind != SignalKind::Event)
                        continue;
                    const int w = signals_[(std::size_t)s].writer;
                    if ((w >= 0 && in[(std::size_t)w]) || (w < 0 && externalAt(s, t)))
                        in[(std::size_t)i] = grew = 1;
                }
            }
        }
        std::vector<int> list;
        for (int i = 0; i < n; ++i)
            if (in[(std::size_t)i]) list.push_back(i);
        return list;
    };

    InputFrame frame;
    auto stepOne = [&](int i, Tick to, StepResult& r) {
        auto& e = participants_[(std::size_t)i];
        buildFrame(i, to, frame);
        Sink sink;
        r = e.participant->doStep(e.lastCommit, to, frame, sink);
        e.trialEvents = std::move(sink.emissions);
    };
    auto rollbackAll = [&](const std::vector<int>& list) {
        for (int s : list)
        {
            auto& e = participants_[(std::size_t)s];
            if (e.info.caps.canRollback)
                e.participant->rollback();
            e.trialEvents.clear();
        }
    };

    // 2a. Event locators first; one that stops early shortens the round.
    std::vector<int> stepped;
    bool pauseRequested = false;
    int iterations = 0;
    for (bool shortened = true; shortened;)
    {
        shortened = false;
        for (int i : dueAt(tNext, stepped))
        {
            auto& e = participants_[(std::size_t)i];
            if (!e.info.caps.locatesEvents || std::find(stepped.begin(), stepped.end(), i) != stepped.end())
                continue;
            StepResult r;
            stepOne(i, tNext, r);
            if (!r.status)
            {
                rollbackAll(stepped);
                return faultRound(e.info.name, Category::Convergence, "step to " + describeTick(tNext) + " failed: " + r.status.message);
            }
            pauseRequested |= r.pauseRequested;
            if (r.stoppedEarly && r.stoppedAt < tNext)
            {
                if (r.stoppedAt <= e.lastCommit)
                {
                    stepped.push_back(i);
                    rollbackAll(stepped);
                    return faultRound(e.info.name, Category::Events, "located an event at " + describeTick(r.stoppedAt) + ", not after the start of its step (" + describeTick(e.lastCommit) + ").");
                }
                if (++iterations > locatorLimit_)
                {
                    stepped.push_back(i);
                    rollbackAll(stepped);
                    return faultRound(e.info.name, Category::Synchronisation, "event location did not settle within " + std::to_string(locatorLimit_) + " iterations.");
                }
                // Earlier locators stepped past the event: roll them back and step them again.
                if (!stepped.empty())
                    note(Severity::Info, Category::Synchronisation, e.info.name,
                         "located an event at " + describeTick(r.stoppedAt) + "; " + std::to_string(stepped.size()) + " participant(s) rolled back and stepped again.");
                rollbackAll(stepped);
                stepped.assign(1, i);
                tNext = r.stoppedAt;
                shortened = true;
                break;
            }
            stepped.push_back(i);
        }
    }

    // 2b. Other participants that can roll back, then those that cannot (C1).
    const auto finalDue = dueAt(tNext, stepped);
    for (int pass = 0; pass < 2; ++pass)
        for (int i : finalDue)
        {
            auto& e = participants_[(std::size_t)i];
            if (e.info.caps.locatesEvents || (pass == 0) != e.info.caps.canRollback)
                continue;
            StepResult r;
            stepOne(i, tNext, r);
            stepped.push_back(i);
            if (!r.status)
            {
                rollbackAll(stepped);
                return faultRound(e.info.name, Category::Convergence, "step to " + describeTick(tNext) + " failed: " + r.status.message);
            }
            if (r.stoppedEarly && r.stoppedAt < tNext)
            {
                rollbackAll(stepped);
                return faultRound(e.info.name, Category::Configuration, "stopped its step early at " + describeTick(r.stoppedAt) + " but does not declare that it locates events.");
            }
            pauseRequested |= r.pauseRequested;
        }

    // 3. Check every trial, then commit them together.
    for (int i : stepped)
    {
        auto& e = participants_[(std::size_t)i];
        if (auto s = readOutputs(i, true); !s)
        {
            rollbackAll(stepped);
            return faultRound(e.info.name, Category::Numerical, s.message + " (step to " + describeTick(tNext) + ")");
        }
        for (const auto& em : e.trialEvents)
        {
            if (em.port < 0 || em.port >= (int)e.info.outputs.size() || e.info.outputs[(std::size_t)em.port].kind != SignalKind::Event)
            {
                rollbackAll(stepped);
                return faultRound(e.info.name, Category::Events, "emitted on port " + std::to_string(em.port) + ", which is not one of its event outputs.");
            }
            if (em.at < tNext || em.at > kLastSchedulable)
            {
                rollbackAll(stepped);
                return faultRound(e.info.name, Category::Events, "emitted an event at " + describeTick(em.at) + ", outside [end of its step " + describeTick(tNext) + ", clock range].");
            }
        }
    }
    for (int i : stepped)
    {
        auto& e = participants_[(std::size_t)i];
        e.participant->commit();
        e.lastCommit = tNext;
        publishOutputs(i, tNext, tNext, 0, false);
        for (const auto& em : e.trialEvents)
        {
            Event out;
            out.time = { em.at, 0 };
            out.priority = (int)EventPriority::Participant;
            out.source = i;
            out.signal = e.outputs[(std::size_t)em.port];
            out.payload = em.payload;
            pushEvent(out); // validated above
        }
        e.trialEvents.clear();
    }
    clock_ = { tNext, 0 };

    // External inputs ride on this boundary (C8); replayed ones must land on the same boundary.
    for (auto& ev : pendingBoundary_)
    {
        ev.time = { tNext, 0 };
        pushEvent(ev);
    }
    pendingBoundary_.clear();
    while (!recordedBoundary_.empty() && recordedBoundary_.front().time.tick <= tNext)
    {
        Event ev = recordedBoundary_.front();
        recordedBoundary_.erase(recordedBoundary_.begin());
        if (ev.time.tick < tNext)
            return faultRound({}, Category::Events, "replay diverged: a recorded input at " + describeTick(ev.time.tick) + " fell between round boundaries (this round ends at " + describeTick(tNext) + ").");
        ev.time.microstep = 0;
        pushEvent(ev);
    }

    // 4. Events at this time.
    int processed = 0;
    if (auto s = processEvents(tNext, processed); !s)
        return faultRound({}, Category::Events, s.message);

    // 5. Record and publish.
    ++roundCount_;
    if (recordingStarted_ && recorder_ != nullptr)
    {
        current_.round = roundCount_;
        current_.time = { tNext, 0 };
        recorder_->round(std::move(current_));
    }
    current_ = {};
    publishSnapshot();

    outcome.kind = RoundOutcome::Kind::Advanced;
    outcome.time = clock_;
    outcome.eventsProcessed = processed;
    if (pauseRequested)
    {
        state_ = LifecycleState::Paused;
        outcome.kind = RoundOutcome::Kind::Paused;
        note(Severity::Info, Category::Lifecycle, {}, "paused at " + describeTick(tNext) + " at a participant's request.");
    }
    return outcome;
}

void Simulation::publishSnapshot()
{
    auto& s = snapshots_.back();
    s.tick = clock_.tick;
    s.round = roundCount_;
    s.values.resize(committed_.size());
    for (std::size_t i = 0; i < committed_.size(); ++i)
        s.values[i] = committed_[i].value;
    snapshots_.publish();
}

Status Simulation::runUntil(Tick t)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Running, "run"); !s)
        return s;
    if (t > kLastSchedulable)
        return fail(Category::Lifecycle, {}, "cannot run to " + describeTick(t) + ": beyond the clock's range.");
    while (state_ == LifecycleState::Running)
    {
        const auto next = peekNextRoundTime();
        if (next == kNever || next > t)
            break;
        const auto out = advanceRound();
        if (out.kind == RoundOutcome::Kind::Faulted)
            return Status::failure(lastError_);
        if (out.kind == RoundOutcome::Kind::Idle)
            break;
    }
    return {};
}

Status Simulation::runToNextEvent(Tick limit)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Running, "run"); !s)
        return s;
    while (state_ == LifecycleState::Running)
    {
        const auto next = peekNextRoundTime();
        if (next == kNever || next > limit)
            break;
        const auto out = advanceRound();
        if (out.kind == RoundOutcome::Kind::Faulted)
            return Status::failure(lastError_);
        if (out.kind == RoundOutcome::Kind::Idle || out.eventsProcessed > 0)
            break;
    }
    return {};
}

Status Simulation::runUntilCondition(const std::function<bool(const Simulation&)>& condition, Tick limit)
{
    if (auto s = refuseUnless(state_ == LifecycleState::Running, "run"); !s)
        return s;
    while (state_ == LifecycleState::Running)
    {
        const auto next = peekNextRoundTime();
        if (next == kNever || next > limit)
            break;
        const auto out = advanceRound();
        if (out.kind == RoundOutcome::Kind::Faulted)
            return Status::failure(lastError_);
        if (out.kind == RoundOutcome::Kind::Idle || condition(*this))
            break;
    }
    return {};
}
}

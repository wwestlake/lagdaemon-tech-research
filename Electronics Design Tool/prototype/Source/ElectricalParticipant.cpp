#include "ElectricalParticipant.h"

#include <cmath>
#include <cstring>

namespace electrical
{
namespace
{
using circuit_sim::Element;

bool isSource(Element::Type t) { return t == Element::Type::VoltageSource || t == Element::Type::CurrentSource; }
bool isResistance(Element::Type t) { return t == Element::Type::Resistor || t == Element::Type::VariableResistor || t == Element::Type::Switch; }
bool isControlledSwitch(Element::Type t)
{
    return t == Element::Type::VoltageControlledSwitch || t == Element::Type::CurrentControlledSwitch;
}

constexpr std::uint32_t kStateMagic = 0x53454C45; // "ELES"
constexpr std::uint32_t kStateVersion = 1;

class Writer
{
public:
    std::vector<std::uint8_t> bytes;
    template <typename T> void put(const T& v)
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template <typename T> void putVector(const std::vector<T>& v)
    {
        put<std::uint64_t>(v.size());
        if (!v.empty())
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            bytes.insert(bytes.end(), p, p + v.size() * sizeof(T));
        }
    }
};

class Reader
{
public:
    explicit Reader(const std::vector<std::uint8_t>& b) : bytes(b) {}
    template <typename T> bool get(T& v)
    {
        if (at + sizeof(T) > bytes.size()) return false;
        std::memcpy(&v, bytes.data() + at, sizeof(T));
        at += sizeof(T);
        return true;
    }
    template <typename T> bool getVector(std::vector<T>& v)
    {
        std::uint64_t n = 0;
        if (!get(n) || n > (bytes.size() - at) / sizeof(T)) return false;
        v.resize((size_t)n);
        if (n > 0) std::memcpy(v.data(), bytes.data() + at, (size_t)n * sizeof(T));
        at += (size_t)n * sizeof(T);
        return true;
    }
    bool done() const { return at == bytes.size(); }

private:
    const std::vector<std::uint8_t>& bytes;
    size_t at = 0;
};
}

std::vector<std::uint8_t> serialize(const circuit_sim::TransientStepper::State& s, sim::Tick t0)
{
    Writer w;
    w.put(kStateMagic);
    w.put(kStateVersion);
    w.put(t0);
    w.put(s.time);
    w.putVector(s.x);
    w.putVector(s.reactiveV);
    w.putVector(s.reactiveI);
    w.putVector(s.switches);
    w.put<std::uint8_t>(s.restartPending ? 1 : 0);
    w.put<std::uint64_t>(s.inputs.size());
    for (const auto& in : s.inputs)
    {
        w.put<std::uint8_t>(in.set ? 1 : 0); // field by field: no padding bytes in the blob
        w.put(in.t0);
        w.put(in.v0);
        w.put(in.t1);
        w.put(in.v1);
    }
    w.put<std::uint64_t>(s.devices.size());
    for (const auto& d : s.devices)
        w.putVector(d);
    return std::move(w.bytes);
}

bool deserialize(const std::vector<std::uint8_t>& blob, circuit_sim::TransientStepper::State& s, sim::Tick& t0, std::string& error)
{
    Reader r(blob);
    std::uint32_t magic = 0, version = 0;
    if (!r.get(magic) || magic != kStateMagic)
    {
        error = "not an electrical participant state";
        return false;
    }
    if (!r.get(version) || version != kStateVersion)
    {
        error = "electrical state version " + std::to_string(version) + " is not supported (this build reads version "
              + std::to_string(kStateVersion) + ")";
        return false;
    }
    std::uint8_t restart = 0;
    std::uint64_t inputs = 0, devices = 0;
    bool ok = r.get(t0) && r.get(s.time) && r.getVector(s.x) && r.getVector(s.reactiveV) && r.getVector(s.reactiveI)
           && r.getVector(s.switches) && r.get(restart) && r.get(inputs) && inputs <= blob.size();
    s.restartPending = restart != 0;
    if (ok)
    {
        s.inputs.assign((size_t)inputs, {});
        for (auto& in : s.inputs)
        {
            std::uint8_t set = 0;
            ok = ok && r.get(set) && r.get(in.t0) && r.get(in.v0) && r.get(in.t1) && r.get(in.v1);
            in.set = set != 0;
        }
        ok = ok && r.get(devices) && devices <= blob.size();
    }
    if (ok)
    {
        s.devices.assign((size_t)devices, {});
        for (auto& d : s.devices)
            ok = ok && r.getVector(d);
    }
    if (!ok || !r.done())
    {
        error = "the electrical state is truncated or malformed";
        return false;
    }
    return true;
}

Participant::Participant(circuit_sim::Circuit circuit, Config config)
    : circuit_(std::move(circuit)), config_(std::move(config)),
      stepper_(circuit_, config_.options,
               circuit_sim::TransientStepper::Settings { config_.locateEvents, config_.locationTolerance, 1.0 / (double)sim::kTicksPerSecond, true })
{
    for (size_t i = 0; i < config_.events.size(); ++i)
    {
        stepper_.addWatch(config_.events[i].watch);
        watchPort_.push_back((int)(config_.outputs.size() + i));
    }
    // Local event scope: a switch whose state is not an output changes inside
    // a step; only changes the bus can see end the round early.
    const auto& parts = circuit_.elements();
    for (size_t i = 0; i < parts.size(); ++i)
        if (isControlledSwitch(parts[i].type))
        {
            bool reported = false;
            for (const auto& o : config_.outputs)
                reported |= o.kind == OutputBinding::Kind::SwitchState && o.element == (int)i;
            stepper_.setSwitchStops((int)i, reported);
        }
}

sim::ParticipantInfo Participant::describe() const
{
    sim::ParticipantInfo info;
    info.name = config_.name;
    const auto& parts = circuit_.elements();
    std::vector<std::string> inputNames;
    for (const auto& b : config_.inputs)
    {
        sim::PortSpec p;
        if (b.kind == InputBinding::Kind::SwitchClosed)
            p = sim::PortSpec::discrete(b.port, sim::ValueType::Boolean);
        else
        {
            std::string unit = "ohm";
            if (b.kind == InputBinding::Kind::SourceValue)
                unit = b.element >= 0 && b.element < (int)parts.size() && parts[(size_t)b.element].type == Element::Type::CurrentSource ? "A" : "V";
            p = sim::PortSpec::continuous(b.port, unit);
            p.policy = sim::InputPolicy::Extrapolate;
        }
        p.required = false; // unbound: the element as the circuit has it
        info.inputs.push_back(p);
        inputNames.push_back(b.port);
    }
    for (const auto& b : config_.outputs)
    {
        sim::PortSpec p;
        if (b.kind == OutputBinding::Kind::SwitchState)
            p = sim::PortSpec::discrete(b.port, sim::ValueType::Boolean);
        else
            p = sim::PortSpec::continuous(b.port, b.kind == OutputBinding::Kind::NodeVoltage ? "V" : "A");
        p.dependsOn = inputNames;
        info.outputs.push_back(p);
    }
    for (const auto& e : config_.events)
        info.outputs.push_back(sim::PortSpec::event(e.port));
    info.timing.kind = sim::TimingKind::VariableStep;
    info.timing.step = config_.step;
    info.timing.minStep = 1;
    info.caps.canRollback = true;
    info.caps.locatesEvents = config_.locateEvents;
    info.caps.stateSerializable = true;
    info.caps.deterministic = true;
    return info;
}

sim::Status Participant::configure(const sim::ParticipantConfig&)
{
    const auto& parts = circuit_.elements();
    auto element = [&](int index, const std::string& port, auto&& typeOk, const char* what) -> sim::Status {
        if (index < 0 || index >= (int)parts.size())
            return sim::Status::failure(config_.name + "." + port + ": element " + std::to_string(index) + " is not in the circuit.");
        if (!typeOk(parts[(size_t)index].type))
            return sim::Status::failure(config_.name + "." + port + ": " + parts[(size_t)index].name + " is not " + what + ".");
        return {};
    };
    for (const auto& b : config_.inputs)
    {
        sim::Status s;
        if (b.kind == InputBinding::Kind::SourceValue)
            s = element(b.element, b.port, isSource, "an independent source");
        else
            s = element(b.element, b.port, isResistance, "a resistor, potentiometer or switch");
        if (!s) return s;
    }
    for (const auto& b : config_.outputs)
    {
        if (b.kind == OutputBinding::Kind::NodeVoltage)
        {
            if (b.plus < 0 || b.plus >= circuit_.nodeCount() || b.minus < 0 || b.minus >= circuit_.nodeCount())
                return sim::Status::failure(config_.name + "." + b.port + ": node out of range.");
            continue;
        }
        auto any = [](Element::Type) { return true; };
        auto s = b.kind == OutputBinding::Kind::SwitchState ? element(b.element, b.port, isControlledSwitch, "a controlled switch")
                                                            : element(b.element, b.port, any, "");
        if (!s) return s;
    }
    if (config_.step <= 0)
        return sim::Status::failure(config_.name + ": the step must be positive.");
    return {};
}

sim::Status Participant::applyInputs(const sim::InputFrame& inputs, sim::Tick at)
{
    for (int p = 0; p < (int)config_.inputs.size(); ++p)
    {
        const auto& b = config_.inputs[(size_t)p];
        if (!inputs.bound(p))
        {
            stepper_.clearInput(b.element);
            continue;
        }
        bool ok = true;
        switch (b.kind)
        {
            case InputBinding::Kind::SourceValue: ok = stepper_.setSourceValue(b.element, inputs.real(p, at)); break;
            case InputBinding::Kind::Resistance: ok = stepper_.setResistance(b.element, inputs.real(p, at)); break;
            case InputBinding::Kind::SwitchClosed: ok = stepper_.setResistance(b.element, inputs.value(p).asBoolean() ? b.onOhms : b.offOhms); break;
        }
        if (!ok)
            return sim::Status::failure(config_.name + "." + b.port + ": " + stepper_.error());
    }
    return {};
}

sim::Status Participant::initialize(sim::Tick t0, int, const sim::InputFrame& inputs)
{
    t0_ = t0;
    if (auto s = applyInputs(inputs, t0); !s)
        return s;
    if (!stepper_.init())
        return sim::Status::failure(config_.name + ": " + stepper_.error());
    if (!stepper_.saveState(committed_))
        return sim::Status::failure(config_.name + ": " + stepper_.error());
    return {};
}

sim::StepResult Participant::doStep(sim::Tick from, sim::Tick to, const sim::InputFrame& inputs, sim::EventSink& events)
{
    sim::StepResult result;
    if (!stepper_.saveState(committed_))
    {
        result.status = sim::Status::failure(config_.name + ": " + stepper_.error());
        return result;
    }
    // Sources follow their inputs linearly across the step (the values at
    // both ends from the inputs' extrapolation), so a located crossing
    // inside the step sees the input at that time.
    for (int p = 0; p < (int)config_.inputs.size(); ++p)
    {
        const auto& b = config_.inputs[(size_t)p];
        if (b.kind == InputBinding::Kind::SourceValue && inputs.bound(p))
            stepper_.setSourceRamp(b.element, seconds(from), inputs.real(p, from), seconds(to), inputs.real(p, to));
        else if (!inputs.bound(p))
            stepper_.clearInput(b.element);
        else
        {
            const auto ohms = b.kind == InputBinding::Kind::SwitchClosed ? (inputs.value(p).asBoolean() ? b.onOhms : b.offOhms)
                                                                         : inputs.real(p, to);
            stepper_.setResistance(b.element, ohms);
        }
    }
    auto& located = located_;
    located.clear();
    if (!stepper_.advance(seconds(to), &located))
    {
        result.status = sim::Status::failure(config_.name + ": " + stepper_.error() + " (step " + sim::describeTick(from) + " to "
                                             + sim::describeTick(to) + ")");
        return result;
    }
    auto tickOf = [&](double seconds) {
        auto t = t0_ + (sim::Tick)std::llround(seconds * (double)sim::kTicksPerSecond);
        return std::min(std::max(t, from + 1), to);
    };
    if (stepper_.time() < seconds(to))
    {
        const auto at = tickOf(stepper_.time());
        if (at < to)
        {
            result.stoppedEarly = true;
            result.stoppedAt = at;
        }
    }
    for (size_t k = 0; k < located.size(); ++k)
        if (const auto& e = located[k]; e.watch >= 0 && e.watch < (int)watchPort_.size())
            events.emit(watchPort_[(size_t)e.watch], tickOf(e.time), { e.direction, 0.0 });
    return result;
}

void Participant::commit() {}

void Participant::rollback()
{
    stepper_.restoreState(committed_);
    ++rollbacks_;
}

void Participant::getOutputs(sim::OutputFrame& out) const
{
    for (int p = 0; p < (int)config_.outputs.size(); ++p)
    {
        const auto& b = config_.outputs[(size_t)p];
        switch (b.kind)
        {
            case OutputBinding::Kind::NodeVoltage: out.setReal(p, stepper_.voltage(b.plus) - stepper_.voltage(b.minus)); break;
            case OutputBinding::Kind::BranchCurrent: out.setReal(p, stepper_.branchCurrent(b.element)); break;
            case OutputBinding::Kind::SwitchState: out.set(p, sim::Value::ofBoolean(stepper_.switchClosed(b.element))); break;
        }
    }
}

std::vector<std::uint8_t> Participant::saveState() const
{
    circuit_sim::TransientStepper::State s;
    if (!stepper_.saveState(s))
        return {};
    return serialize(s, t0_);
}

sim::Status Participant::restoreState(const std::vector<std::uint8_t>& blob)
{
    circuit_sim::TransientStepper::State s;
    sim::Tick t0 = 0;
    std::string error;
    if (!deserialize(blob, s, t0, error))
        return sim::Status::failure(config_.name + ": " + error);
    if (!stepper_.restoreState(s))
        return sim::Status::failure(config_.name + ": " + stepper_.error());
    t0_ = t0;
    committed_ = std::move(s);
    return {};
}
}

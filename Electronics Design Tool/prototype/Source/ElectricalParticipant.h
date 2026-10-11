#pragma once

// The electrical participant (system simulator, phase P1): the internal
// circuit solver's TransientStepper as a sim_core participant.
//
// Plain C++ (sim_core and circuit_sim, no JUCE), so it runs headless in tests
// and in the Workbench alike. The Workbench turns a diagram into a Circuit
// and the bindings below; this adapter knows nothing about diagrams.
//
// - Inputs (live): an independent source's value (V or A), a resistance
//   (resistor, potentiometer leg; ohm) or a manual switch's state (boolean,
//   closed = onOhms, open = offOhms). An unbound input leaves the element as
//   the circuit has it (a source keeps its own waveform). Continuous inputs
//   are read at the end of each step, the time the trapezoidal step uses.
// - Outputs: node voltages (V), branch currents (A) and controlled-switch
//   states (boolean). Every output is taken to depend on every input without
//   delay (a resistive circuit does), so feedthrough loops are refused (C2);
//   a sampled controller in the loop breaks them.
// - Events: watched thresholds become event outputs. With event location on
//   (the default) a crossing inside a step ends the trial just past it
//   (stoppedEarly), at a whole tick (C9). Switch thresholds are located too;
//   a switch change ends the trial only when its state is an output (local
//   event scope: a change nothing outside the circuit sees stays inside the
//   step).
// - Rollback and checkpoints use the stepper's whole-state copies (G1): the
//   state is saved at the start of every trial and restored on rollback.

#include "CircuitSolver.h"

#include "sim_core/Participant.h"

#include <string>
#include <vector>

namespace electrical
{
struct InputBinding
{
    enum class Kind { SourceValue, Resistance, SwitchClosed };
    Kind kind = Kind::SourceValue;
    std::string port;
    int element = -1;
    double onOhms = 1e-3, offOhms = 1e9; // SwitchClosed
};

struct OutputBinding
{
    enum class Kind { NodeVoltage, BranchCurrent, SwitchState };
    Kind kind = Kind::NodeVoltage;
    std::string port;
    circuit_sim::Node plus = 0, minus = 0; // NodeVoltage: V(plus) - V(minus)
    int element = -1;                      // BranchCurrent, SwitchState
};

struct EventBinding
{
    std::string port;
    circuit_sim::TransientStepper::Watch watch; // payload: code = +1 rising / -1 falling
};

struct Config
{
    std::string name = "electrical";
    sim::Tick step = 20'000'000;  // the largest step (20 us)
    bool locateEvents = true;
    double locationTolerance = 1e-9;
    circuit_sim::Options options;
    std::vector<InputBinding> inputs;
    std::vector<OutputBinding> outputs;
    std::vector<EventBinding> events;
};

class Participant final : public sim::Participant
{
public:
    Participant(circuit_sim::Circuit circuit, Config config);

    sim::ParticipantInfo describe() const override;
    sim::Status configure(const sim::ParticipantConfig&) override;
    sim::Status initialize(sim::Tick t0, int pass, const sim::InputFrame& inputs) override;
    sim::StepResult doStep(sim::Tick from, sim::Tick to, const sim::InputFrame& inputs, sim::EventSink& events) override;
    void commit() override;
    void rollback() override;
    void getOutputs(sim::OutputFrame& out) const override;
    std::vector<std::uint8_t> saveState() const override;
    sim::Status restoreState(const std::vector<std::uint8_t>& blob) override;

    const circuit_sim::TransientStepper& stepper() const { return stepper_; }
    std::uint64_t rollbacks() const { return rollbacks_; }

private:
    sim::Status applyInputs(const sim::InputFrame& inputs, sim::Tick at);
    double seconds(sim::Tick t) const { return sim::ticksToSeconds(t - t0_); }

    circuit_sim::Circuit circuit_;
    Config config_;
    circuit_sim::TransientStepper stepper_;
    circuit_sim::TransientStepper::State committed_;
    sim::Tick t0_ = 0;
    std::uint64_t rollbacks_ = 0;
    std::vector<int> watchPort_; // per stepper watch: event output port
    std::vector<circuit_sim::TransientStepper::Event> located_; // scratch, reused each step
};

// Byte form of a stepper state (checkpoints, reset). Versioned; refuses a
// blob of another version or circuit size.
std::vector<std::uint8_t> serialize(const circuit_sim::TransientStepper::State& s, sim::Tick t0);
bool deserialize(const std::vector<std::uint8_t>& blob, circuit_sim::TransientStepper::State& s, sim::Tick& t0, std::string& error);
}

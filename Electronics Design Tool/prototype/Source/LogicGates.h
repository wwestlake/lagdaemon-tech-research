#pragma once

// Logic gates for the internal electrical solver (circuit_sim).
//
// One logical function per gate (truth()), shared by every use: the
// transient model evaluates it on input logic levels (0 or 1), the DC model
// on soft levels in between (its multilinear extension, which equals the
// truth table at 0 and 1 and is smooth between). Nothing else holds a truth
// table.
//
// GateDevice is the electrical model, a circuit_sim::ProgrammableDevice with
// pins in0..in(n-1), out:
// - inputs: a resistance rIn to ground each; a logic level from the
//   switching threshold vth, with hysteresis (rising at vth + hys/2, falling
//   at vth - hys/2);
// - output: a level (vol or voh) behind rOut, referred to ground. First-pass
//   simplification: the gate has no supply pins; its levels come from the
//   model (an ideal supply), it draws no supply current, and the output does
//   not sag with a supply;
// - DC (operating point, sweeps, AC bias): the output follows the inputs
//   with no delay through a smooth transfer curve of finite gain (dcGain at
//   the threshold), so Newton has a slope to work with and an input held at
//   the threshold gives a mid-level output, as a real gate's would;
// - transient: an input crossing its threshold (time interpolated inside the
//   accepted step) that changes the gate's logic result schedules the output
//   change at the crossing + tpd, a device event (the stepper lands on it
//   exactly). The output then moves linearly to the new level over tr (also
//   an event, at the end). Delay semantics: inertial. A new input change
//   cancels a change still pending, so a pulse shorter than tpd does not
//   reach the output; a change already started (in its transition) runs on.
//   Pending changes are committed state: saved, restored, never consumed by
//   a rejected step.

#include "CircuitSolver.h"

#include <string>
#include <vector>

namespace logic
{
enum class Function { Not, Buffer, And, Nand, Or, Nor, Xor, Xnor };

// "not", "buffer", "and", "nand", "or", "nor", "xor", "xnor" (case-insensitive).
bool parseFunction(const std::string& name, Function& out);
const char* functionName(Function f);
// 1 for NOT and BUFFER; 2..8 otherwise.
int minInputs(Function f);
int maxInputs(Function f);

// The logical function on levels in [0, 1]: the multilinear extension of the
// truth table (AND = product, OR = 1 - product of complements, XOR = odd
// parity p + q - 2pq folded over the inputs; NAND, NOR, XNOR, NOT invert).
double truth(Function f, const double* levels, int count);
bool truth(Function f, const bool* levels, int count);

struct Model
{
    double vol = 0.0, voh = 5.0;   // output levels (V)
    double vth = 2.5;              // input switching threshold (V)
    double hysteresis = 0.0;       // V: rising at vth + h/2, falling at vth - h/2
    double rOut = 50.0;            // output resistance (ohm)
    double rIn = 1e9;              // input resistance to ground (ohm)
    double tpd = 10e-9;            // propagation delay (s), > 0
    double tr = 4e-9;              // output transition time (s), >= 0
    double dcGain = 25.0;          // DC transfer slope at the threshold (V/V)
};

// Empty when usable, else what is wrong.
std::string problemsWith(const Model& m, Function f, int inputs);

class GateDevice final : public circuit_sim::ProgrammableDevice
{
public:
    GateDevice(Function f, int inputs, Model model);

    void reset() override;
    bool evaluate(double t, double h, const std::vector<double>& pinVoltages, std::vector<double>& pinCurrents,
                  std::string& error) override;
    void accept(double t) override;
    bool saveState(std::vector<std::uint8_t>& out) const override;
    bool restoreState(const std::vector<std::uint8_t>& in) override;
    bool schedulesEvents() const override { return true; }
    double nextEventTime(double after) const override;

    // The committed state (tests, diagnostics).
    bool outputHigh() const { return committed.target; }
    bool hasPending() const { return committed.pendingAt >= 0.0; }
    double pendingTime() const { return committed.pendingAt; }

private:
    struct State
    {
        double t = -1.0;                 // last accepted time (< 0: none yet: DC)
        std::vector<double> vin;         // input voltages at t
        std::vector<signed char> in;     // input logic levels at t
        bool target = false;             // output level the transition goes to
        double rampStart = -1.0;         // the transition to `target` started here (< 0: settled)
        double from = 0.0;               // from this voltage
        double pendingAt = -1.0;         // a change not started yet (< 0: none)
        bool pendingTarget = false;
    };
    double levelAt(const State& s, double t) const;
    void startDue(State& s, double start) const;
    bool logicOut(const std::vector<signed char>& in) const;

    Function function;
    int inputs;
    Model model;
    State committed, trial;
    std::vector<double> scratchLevels;
};
}

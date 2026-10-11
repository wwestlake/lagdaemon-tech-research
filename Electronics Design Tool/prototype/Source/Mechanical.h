#pragma once

// Electromechanical devices for the internal electrical solver (circuit_sim).
//
// Mechanical state is part of the one circuit the solver integrates: there
// is no second solver and no second clock. A rotating shaft is a node of the
// circuit through the mobility (torque-current) analogy, in SI units:
//
//   shaft node value       angular velocity omega   rad/s     (1 "V" = 1 rad/s)
//   current into the node  torque on the shaft      N*m       (1 "A" = 1 N*m)
//   capacitance to ground  rotational inertia J     kg*m^2    (1 "F" = 1 kg*m^2)
//   conductance to ground  viscous damping B       N*m*s/rad (1 "S" = 1 N*m*s/rad)
//   current source out     a load torque            N*m
//
// so J d(omega)/dt = (sum of torques into the shaft) is KCL at the shaft
// node, integrated by the same trapezoidal / backward-Euler steps, stepped,
// rejected and rolled back (TransientStepper) with the rest of the circuit.
// Every device on a shaft adds its elements to that one node; a later
// mechanical part (gear, brake, second motor) shares a shaft by using the
// same node. Shaft and angle nodes are quantity nodes
// (Circuit::addQuantityNode): their values are not voltages, so Newton's
// voltage step limit does not apply to them.
//
// The shaft angle theta = integral of omega is a second quantity node,
// driven by ShaftDevice (a circuit_sim::ProgrammableDevice), which also
// carries the shaft's nonlinear torques: Coulomb friction and end stops. It
// integrates theta with the trapezoidal rule from its committed state,
// implicitly (the trial angle follows the trial speed inside Newton), and
// keeps theta and omega as committed state: saved, restored, never advanced
// by a rejected step. In DC (operating point, sweeps) the shaft turns at its
// steady speed and the angle is held at its initial value.
//
// A brushed DC motor (addDcMotor) is then ordinary elements:
//
//   +  --Ra--+--La--+--[E: Ke*omega]--  -        armature (La's branch current is I)
//   shaft:  F: Kt*I into the shaft, C = J (initial condition omega0),
//           1/B to ground, the load torque out of the shaft, ShaftDevice
//
// V = Ra I + La dI/dt + Ke omega and J d(omega)/dt = Kt I - B omega - Tload
// hold exactly as circuit equations: speed changes the current through the
// back EMF, current changes the speed through the torque, and power flows
// either way (motoring or generating) as the equations give it.

#include "CircuitSolver.h"

#include <string>
#include <vector>

namespace mech
{
// Nonlinear shaft torques and the angle. Pins: shaft (omega), angle (theta).
struct ShaftModel
{
    double theta0 = 0.0;          // initial angle (rad)
    double omega0 = 0.0;          // initial speed (rad/s): the shaft inertia's initial condition too
    double coulomb = 0.0;         // Coulomb friction torque (N*m, >= 0), opposing the motion
    double smoothing = 0.1;       // rad/s: friction is coulomb * tanh(omega / smoothing) (no stiction)
    bool hasMin = false, hasMax = false;
    double minAngle = 0.0, maxAngle = 0.0; // end stops (rad)
    double stopStiffness = 100.0; // N*m/rad, while the shaft is past a stop
    double stopDamping = 0.01;    // N*m*s/rad, while past a stop (the contact torque never pulls)
};

std::string problemsWith(const ShaftModel& m);

class ShaftDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit ShaftDevice(ShaftModel model);

    void reset() override;
    bool evaluate(double t, double h, const std::vector<double>& pinVoltages, std::vector<double>& pinCurrents,
                  std::string& error) override;
    void accept(double t) override;
    bool saveState(std::vector<std::uint8_t>& out) const override;
    bool restoreState(const std::vector<std::uint8_t>& in) override;

    // The torque the nonlinear parts put on the shaft at this angle and speed.
    double torque(double theta, double omega) const;
    double angle() const { return theta; }

private:
    ShaftModel m;
    double theta = 0.0, omega = 0.0;               // committed
    double trialTheta = 0.0, trialOmega = 0.0;     // last evaluation
};

// ---- derived quantities ----------------------------------------------------------

// A value computed from a solution: gain * product of factors. A factor is a
// node voltage difference, the current into an element's first terminal
// (circuit_sim::terminalCurrents; a current source gives its waveform at the
// sample's time) or an element's value. Speed, angle, torque, powers and
// energies of a motor are quantities of its elements.
struct Factor
{
    enum class Kind { Voltage, Current, Value };
    Kind kind = Kind::Voltage;
    circuit_sim::Node node = 0, node2 = 0; // Voltage: V(node) - V(node2)
    int element = -1;                     // Current, Value
};

struct Quantity
{
    std::string name;        // "M1.speed"
    std::string unit;        // "rad/s"
    std::string description;
    double gain = 1.0;
    std::vector<Factor> factors;
    // Linear in the solution (one Voltage or Current factor): has an AC value.
    bool linear() const;
};

double valueOf(const Quantity& q, const circuit_sim::Circuit& c, const circuit_sim::OperatingPoint& solution, double t);

// ---- brushed DC motor ------------------------------------------------------------

struct DcMotorModel
{
    double ra = 2.0;      // armature resistance (ohm, > 0)
    double la = 1e-3;     // armature inductance (H, >= 0)
    double kt = 0.02;     // torque constant (N*m/A, > 0)
    double ke = 0.02;     // back-EMF constant (V*s/rad, > 0); equals kt in SI for a lossless conversion
    double j = 1e-5;      // rotor inertia (kg*m^2, > 0)
    double b = 1e-6;      // rotor viscous damping (N*m*s/rad, >= 0)
    double i0 = 0.0;      // initial armature current (A), when la > 0
};

struct MotorLoad
{
    double inertia = 0.0;             // added to the rotor's (kg*m^2, >= 0)
    double damping = 0.0;             // viscous, added to the rotor's (N*m*s/rad, >= 0)
    circuit_sim::Waveform torque;     // N*m; positive opposes forward rotation (a time-varying load: PWL, pulse...)
};

struct DcMotor
{
    circuit_sim::Node shaft = 0, angle = 0;
    int armature = -1;   // La (or a 0 V sense source when la = 0): its branch current is I, + to -
    int resistance = -1, emf = -1, torque = -1, inertia = -1, damping = -1, load = -1, device = -1;
    std::vector<Quantity> quantities;
};

// Empty when usable, else what is wrong (each problem a sentence).
std::string problemsWith(const DcMotorModel& m, const MotorLoad& load);

// Adds the motor's elements, named "<name>.<part>" with the armature
// inductance named `name` (the part's primary element: I(name) is the
// armature current). Quantities: speed, rpm, position, current, voltage,
// backemf, torque, power_in, power_copper, power_converted, power_damping,
// power_load, energy_kinetic, each "<name>.<key>".
DcMotor addDcMotor(circuit_sim::Circuit& c, const std::string& name, circuit_sim::Node plus, circuit_sim::Node minus,
                   const DcMotorModel& model, const MotorLoad& load, const ShaftModel& shaft);

// A tachogenerator on a shaft: out+ - out- = k * omega (V per rad/s) behind
// rOut (ohm). A sensor through the electrical interface.
int addTachometer(circuit_sim::Circuit& c, const std::string& name, circuit_sim::Node shaft, circuit_sim::Node outPlus,
                  circuit_sim::Node outMinus, double voltsPerRadPerSecond, double rOut);

// A position sensor (potentiometer or encoder) on a shaft: out+ - out- = k * theta (V per rad) behind
// rOut (ohm). A sensor through the electrical interface.
int addPositionSensor(circuit_sim::Circuit& c, const std::string& name, circuit_sim::Node angle, circuit_sim::Node outPlus,
                      circuit_sim::Node outMinus, double voltsPerRadian, double rOut);
} // namespace mech

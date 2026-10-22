#pragma once

#include "Mechanical.h"
#include "RoboticsCatalog.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mech
{
// ==============================================================================
// 1. TI DRV8833 / Pololu #2130 Dual H-Bridge Motor Driver
// ==============================================================================
// Pins (12):
//   0: VM      (Motor supply 2.7 V .. 10.8 V)
//   1: GND     (Ground reference)
//   2: AIN1    (Bridge A control input 1)
//   3: AIN2    (Bridge A control input 2)
//   4: BIN1    (Bridge B control input 1)
//   5: BIN2    (Bridge B control input 2)
//   6: nSLEEP  (Active-low sleep input; internal 500k pulldown on IC, 47k pullup to VM on Pololu #2130)
//   7: nFAULT  (Open-drain active-low fault output)
//   8: AOUT1   (Bridge A output 1)
//   9: AOUT2   (Bridge A output 2)
//  10: BOUT1   (Bridge B output 1)
//  11: BOUT2   (Bridge B output 2)
struct Drv8833Model
{
    double minSupply = 2.7;          // UVLO rising threshold [V]
    double maxSupply = 10.8;         // Max recommended operating VM [V]
    double rdsOn = 0.36;             // Total HS + LS FET on-resistance [ohm] (0.18 ohm per switch)
    double currentLimit = 1.5;       // Bridge current limit [A] (1.5 A RMS TI IC, 1.2 A continuous Pololu #2130, up to 2.0 A peak)
    double logicHigh = 2.0;          // Logic input high threshold [V]
    double logicLow = 0.8;           // Logic input low threshold [V]
    bool carrierPullupSleep = false; // Pololu #2130 has 47 kOhm pull-up from nSLEEP to VM
};

std::string problemsWith(const Drv8833Model& m);

class Drv8833Device final : public circuit_sim::ProgrammableDevice
{
public:
    explicit Drv8833Device(Drv8833Model model);

    void reset() override {}
    bool evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error) override;
    void accept(double t) override {}
    bool saveState(std::vector<std::uint8_t>& out) const override { out.clear(); return true; }
    bool restoreState(const std::vector<std::uint8_t>& in) override { return in.empty(); }

private:
    Drv8833Model m;
};

int addDrv8833Driver(circuit_sim::Circuit& c, const std::string& name,
                     const std::array<circuit_sim::Node, 12>& pins,
                     const Drv8833Model& model);

// ==============================================================================
// 2. Omron D2F-01F / D2F-01FL Subminiature Snap-Action Limit Switch (SPDT)
// ==============================================================================
// Pins (4):
//   0: COM  (Common terminal)
//   1: NC   (Normally Closed terminal)
//   2: NO   (Normally Open terminal)
//   3: ACT  (Mechanical plunger/lever actuation displacement node [1 V = 1 mm], additive with travelMm)
struct LimitSwitchD2FModel
{
    enum class OverrideMode { AutoFromTravel = 0, ForceUnactuatedNC = 1, ForceActuatedNO = 2 };

    double rOn = 0.1;                     // Initial contact resistance max [ohm] (100 mOhm per D2F datasheet)
    double rOff = 1e9;                    // Open contact insulation resistance [ohm]
    double pretravelMm = 0.5;             // PT: 0.5 mm (D2F-01F) or 2.5 mm (D2F-01FL)
    double overtravelMm = 0.25;           // OT: 0.25 mm (D2F-01F) or 0.55 mm (D2F-01FL)
    double movementDiffMm = 0.1;          // MD hysteresis: 0.1 mm (D2F-01F) or 0.5 mm (D2F-01FL)
    double operatingForceN = 0.74;        // OF max: 0.74 N (D2F-01F) or 0.25 N (D2F-01FL)
    double releaseForceN = 0.15;          // RF min: 0.15 N (D2F-01F) or 0.03 N (D2F-01FL)
    double maxVoltage = 30.0;             // 30 VDC resistive rating (D2F-01)
    double maxCurrent = 0.1;              // 0.1 A resistive rating (D2F-01)
    double travelMm = 0.0;                // Static plunger travel offset [mm]
    OverrideMode overrideMode = OverrideMode::AutoFromTravel;
};

std::string problemsWith(const LimitSwitchD2FModel& m);

class LimitSwitchD2FDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit LimitSwitchD2FDevice(LimitSwitchD2FModel model);

    void reset() override;
    bool evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error) override;
    void accept(double t) override;
    bool saveState(std::vector<std::uint8_t>& out) const override;
    bool restoreState(const std::vector<std::uint8_t>& in) override;

    bool isActuated() const { return actuated; }

private:
    LimitSwitchD2FModel m;
    bool actuated = false;
    bool trialActuated = false;
};

int addLimitSwitchD2F(circuit_sim::Circuit& c, const std::string& name,
                      circuit_sim::Node com, circuit_sim::Node nc, circuit_sim::Node no, circuit_sim::Node act,
                      const LimitSwitchD2FModel& model);

// ==============================================================================
// 3. ams AS5600 / Seeed Grove 12-Bit Contactless Magnetic Rotary Encoder
// ==============================================================================
// Pins (5):
//   0: ANGLE (Mechanical shaft angle input [rad])
//   1: VCC   (Supply 3.0 V .. 5.5 V)
//   2: GND   (Ground reference)
//   3: OUT   (Analog ratiometric or PWM output)
//   4: DIR   (Direction polarity pin: LOW=CW increases code, HIGH=CCW increases code)
struct As5600Model
{
    enum class OutputMode { AnalogFullRange = 0, AnalogReduced10To90 = 1, PwmOutput = 2 };

    double minSupply = 3.0;         // Minimum operating VCC [V]
    double maxSupply = 5.5;         // Maximum operating VCC [V]
    double startAngleDeg = 0.0;     // Programmable ZPOS start angle [deg]
    double stopAngleDeg = 360.0;    // Programmable MPOS stop angle [deg]
    double angleOffsetRad = 0.0;    // Mechanical zero offset [rad]
    double rOut = 100.0;            // Output stage resistance [ohm]
    double pwmFreqHz = 460.0;       // PWM carrier frequency (115, 230, 460, or 920 Hz)
    OutputMode outputMode = OutputMode::AnalogFullRange;
};

std::string problemsWith(const As5600Model& m);

class As5600EncoderDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit As5600EncoderDevice(As5600Model model);

    void reset() override {}
    bool evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error) override;
    void accept(double t) override {}
    bool saveState(std::vector<std::uint8_t>& out) const override { out.clear(); return true; }
    bool restoreState(const std::vector<std::uint8_t>& in) override { return in.empty(); }

    int codeForAngle(double angleRad, bool ccwPositive) const;

private:
    As5600Model m;
};

int addAs5600Encoder(circuit_sim::Circuit& c, const std::string& name,
                     circuit_sim::Node angleNode, circuit_sim::Node vcc, circuit_sim::Node gnd,
                     circuit_sim::Node outNode, circuit_sim::Node dirNode,
                     const As5600Model& model);

// ==============================================================================
// 4. Hobby RC Servos: Positional (SG90, MG90S) and Continuous-Rotation (FS90R)
// ==============================================================================
struct ServoActuator
{
    circuit_sim::Node shaft = 0;   // Output angular velocity [rad/s]
    circuit_sim::Node angle = 0;   // Output shaft angle [rad]
    circuit_sim::Node torque = 0;  // Output electromagnetic torque [N*m]
    int device = -1;
    std::vector<Quantity> quantities;
};

struct PositionalServoModel
{
    double minSupply = 3.5;                 // Minimum functional supply [V] (nominal 4.8-6.0 V)
    double maxSupply = 6.5;                 // Maximum supply before overvoltage error [V]
    double unloadedSpeedDegPerSec4V8 = 500.0; // SG90: 500 deg/s (0.12 s/60 deg); MG90S: 600 deg/s (0.10 s/60 deg)
    double stallTorqueKgCm4V8 = 1.8;        // SG90 & MG90S: 1.8 kgf*cm at 4.8 V
    double minPulseUs = 500.0;              // Pulse width for -maxAngleDeg [us]
    double neutralPulseUs = 1500.0;         // Neutral pulse width (0 rad) [us]
    double maxPulseUs = 2400.0;             // Pulse width for +maxAngleDeg [us]
    double deadbandUs = 1.0;                // Deadband width [us] (SG90/MG90S: 1 us)
    double maxAngleDeg = 90.0;              // Half-travel angle from neutral [deg] (+/- 90 deg = 180 deg span)
    double commandPulseUs = 1500.0;         // Default/DC command pulse width [us] when PWM pin is steady
    double kpRadPerSecPerRad = 60.0;        // Internal position loop proportional velocity gain [1/s]
    double idleCurrentA = 0.010;            // Quiescent current [A]
    double stallCurrentA = 0.650;           // Current at stall torque [A]
    double outputInertia = 1e-5;            // Output-equivalent inertia [kg*m^2]
    double outputDamping = 0.0;             // Added passive viscous damping [N*m*s/rad] (internal velocity loop provides damping)
};

std::string problemsWith(const PositionalServoModel& m, const MotorLoad& load);

// Pins (6): VCC, GND, PWM, shaft (omega), angle (theta), torque
class PositionalServoDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit PositionalServoDevice(PositionalServoModel model);

    void reset() override;
    bool evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error) override;
    void accept(double t) override;
    bool saveState(std::vector<std::uint8_t>& out) const override;
    bool restoreState(const std::vector<std::uint8_t>& in) override;

    double targetAngleRad(double pulseUs) const;

private:
    PositionalServoModel m;
    // State: [0]=pwmHigh, [1]=lastRiseTime, [2]=measuredPulseUs, [3]=committedTargetPulseUs
    std::array<double, 4> state {}, trial {};
};

ServoActuator addPositionalServo(circuit_sim::Circuit& c, const std::string& name,
                                 circuit_sim::Node vcc, circuit_sim::Node gnd, circuit_sim::Node pwm,
                                 const PositionalServoModel& model, const MotorLoad& load, const ShaftModel& initial);

struct ContinuousServoModel
{
    double minSupply = 3.5;                 // Minimum functional supply [V] (nominal 4.8-6.0 V)
    double maxSupply = 6.5;                 // Maximum supply before overvoltage error [V]
    double noLoadRpm4V8 = 80.0;             // FS90R: 80 rpm at 4.8 V
    double noLoadRpm6V = 100.0;             // FS90R: 100 rpm at 6.0 V
    double noLoadCurrent4V8 = 0.070;        // FS90R: 70 mA at 4.8 V
    double noLoadCurrent6V = 0.080;         // FS90R: 80 mA at 6.0 V
    double stallTorqueKgCm4V8 = 1.3;        // FS90R: 1.3 kgf*cm at 4.8 V
    double stallTorqueKgCm6V = 1.5;         // FS90R: 1.5 kgf*cm at 6.0 V
    double stallCurrent4V8 = 0.700;         // FS90R: 700 mA at 4.8 V
    double stallCurrent6V = 0.800;          // FS90R: 800 mA at 6.0 V
    double idleCurrentA = 0.005;            // FS90R: 5 mA at 4.8 V
    double minPulseUs = 700.0;              // Full speed CW pulse limit [us]
    double neutralPulseUs = 1500.0;         // Stop / neutral pulse [us]
    double maxPulseUs = 2300.0;             // Full speed CCW pulse limit [us]
    double deadbandUs = 90.0;               // Deadband width [us] (+/- 45 us around neutral)
    double commandPulseUs = 1500.0;         // Default/DC command pulse width [us] when PWM pin is steady
    double outputInertia = 1e-5;            // Output-equivalent inertia [kg*m^2]
    double outputDamping = 0.0;             // Added passive viscous damping [N*m*s/rad] (internal torque-speed slope provides damping)
};

std::string problemsWith(const ContinuousServoModel& m, const MotorLoad& load);

// Pins (6): VCC, GND, PWM, shaft (omega), angle (theta), torque
class ContinuousServoDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit ContinuousServoDevice(ContinuousServoModel model);

    void reset() override;
    bool evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error) override;
    void accept(double t) override;
    bool saveState(std::vector<std::uint8_t>& out) const override;
    bool restoreState(const std::vector<std::uint8_t>& in) override;

    double normalizedThrottle(double pulseUs) const;

private:
    ContinuousServoModel m;
    // State: [0]=pwmHigh, [1]=lastRiseTime, [2]=measuredPulseUs
    std::array<double, 3> state {}, trial {};
};

ServoActuator addContinuousServo(circuit_sim::Circuit& c, const std::string& name,
                                 circuit_sim::Node vcc, circuit_sim::Node gnd, circuit_sim::Node pwm,
                                 const ContinuousServoModel& model, const MotorLoad& load, const ShaftModel& initial);

} // namespace mech

#include "RoboticsComponents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace mech
{
namespace
{
constexpr double pi = 3.14159265358979323846;

Factor voltage(circuit_sim::Node node)
{
    Factor f;
    f.node = node;
    return f;
}
} // namespace

// ==============================================================================
// 1. TI DRV8833 / Pololu #2130 Dual H-Bridge Motor Driver
// ==============================================================================

std::string problemsWith(const Drv8833Model& m)
{
    for (double v : { m.minSupply, m.maxSupply, m.rdsOn, m.currentLimit, m.logicHigh, m.logicLow })
        if (!std::isfinite(v))
            return "DRV8833 model parameters must be finite.";
    if (!(m.minSupply > 0.0 && m.maxSupply > m.minSupply))
        return "DRV8833 supply voltage limits must satisfy 0 < minSupply < maxSupply.";
    if (!(m.rdsOn > 0.0))
        return "DRV8833 on-resistance rds_on must be positive.";
    if (!(m.currentLimit > 0.0 && m.currentLimit <= 2.0001))
        return "DRV8833 current limit must be in (0, 2.0] A.";
    if (!(m.logicHigh > m.logicLow && m.logicLow >= 0.0))
        return "DRV8833 logic thresholds must satisfy logicHigh > logicLow >= 0.";
    return {};
}

Drv8833Device::Drv8833Device(Drv8833Model model) : m(model)
{
    if (const auto p = problemsWith(m); !p.empty())
        throw std::invalid_argument(p);
}

bool Drv8833Device::evaluate(double, double, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    if (v.size() != 12)
    {
        error = "DRV8833 requires 12 pins (VM, GND, AIN1, AIN2, BIN1, BIN2, nSLEEP, nFAULT, AOUT1, AOUT2, BOUT1, BOUT2).";
        return false;
    }

    const double vGnd = v[1];
    const double vBus = v[0] - vGnd;
    if (vBus < -0.05)
    {
        error = "DRV8833 motor supply VM polarity is reversed.";
        return false;
    }
    if (vBus > m.maxSupply + 0.05)
    {
        error = "DRV8833 motor supply VM exceeds maximum operating rating (10.8 V).";
        return false;
    }

    i.assign(12, 0.0);

    // Control input pull resistors:
    // AIN1, AIN2, BIN1, BIN2 have ~500 kOhm internal pull-down to GND.
    // nSLEEP has 47 kOhm pull-up to VM on Pololu #2130 carrier, or 500 kOhm pull-down to GND on bare TI IC.
    constexpr double rInputPulldown = 500e3;
    for (int pinIdx : { 2, 3, 4, 5 })
    {
        const double iIn = (v[pinIdx] - vGnd) / rInputPulldown;
        i[pinIdx] -= iIn;
        i[1] += iIn;
    }
    if (m.carrierPullupSleep)
    {
        constexpr double rCarrierPullup = 47e3;
        const double iPull = (v[0] - v[6]) / rCarrierPullup;
        i[0] -= iPull;
        i[6] += iPull;
    }
    else
    {
        const double iSleep = (v[6] - vGnd) / rInputPulldown;
        i[6] -= iSleep;
        i[1] += iSleep;
    }

    const double vSleep = v[6] - vGnd;
    const bool sleepMode = (vSleep < m.logicHigh);
    const bool uvlo = (vBus < m.minSupply);
    const bool active = (!sleepMode && !uvlo);

    // Quiescent supply current: ~1.7 mA active, ~1.6 uA in sleep
    if (vBus > 0.1)
    {
        const double iQuiescent = sleepMode ? 1.6e-6 : 1.7e-3;
        i[0] -= iQuiescent;
        i[1] += iQuiescent;
    }

    bool overcurrentFault = false;
    const double rSwitch = std::max(0.5 * m.rdsOn, 1e-3);
    constexpr double rHiZ = 1e9;

    // Evaluate both H-bridges: Bridge A (inputs 2,3 -> outputs 8,9), Bridge B (inputs 4,5 -> outputs 10,11)
    for (int bridge = 0; bridge < 2; ++bridge)
    {
        const int in1Idx = 2 + 2 * bridge;
        const int in2Idx = 3 + 2 * bridge;
        const int out1Idx = 8 + 2 * bridge;
        const int out2Idx = 9 + 2 * bridge;

        const bool in1 = (v[in1Idx] - vGnd) >= m.logicHigh;
        const bool in2 = (v[in2Idx] - vGnd) >= m.logicHigh;

        if (!active || (!in1 && !in2))
        {
            // Coast / fast decay (high-Z with body-diode clamp to rails if inductive kick exceeds rails)
            for (int outIdx : { out1Idx, out2Idx })
            {
                const double iLeak = (v[outIdx] - vGnd) / rHiZ;
                i[outIdx] -= iLeak;
                i[1] += iLeak;

                if (v[outIdx] > v[0])
                {
                    const double iClampHigh = (v[outIdx] - v[0]) / rSwitch;
                    i[outIdx] -= iClampHigh;
                    i[0] += iClampHigh;
                }
                else if (v[outIdx] < vGnd)
                {
                    const double iClampLow = (vGnd - v[outIdx]) / rSwitch;
                    i[outIdx] += iClampLow;
                    i[1] -= iClampLow;
                }
            }
        }
        else if (in1 && in2)
        {
            // Brake / slow decay: both outputs tied to GND via low-side FETs (rSwitch each)
            for (int outIdx : { out1Idx, out2Idx })
            {
                const double iSink = (v[outIdx] - vGnd) / rSwitch;
                i[outIdx] -= iSink;
                i[1] += iSink;
            }
        }
        else
        {
            // Active drive:
            // Forward (IN1=1, IN2=0): OUT1 -> VM via HS (rSwitch), OUT2 -> GND via LS (rSwitch)
            // Reverse (IN1=0, IN2=1): OUT1 -> GND via LS (rSwitch), OUT2 -> VM via HS (rSwitch)
            const int hsOut = in1 ? out1Idx : out2Idx;
            const int lsOut = in1 ? out2Idx : out1Idx;

            double iHs = (v[0] - v[hsOut]) / rSwitch; // current sourced from VM into hsOut
            double iLs = (v[lsOut] - vGnd) / rSwitch; // current sunk from lsOut into GND

            // Current-chopping regulation at +/- m.currentLimit
            if (std::abs(iHs) >= 0.99 * m.currentLimit || std::abs(iLs) >= 0.99 * m.currentLimit)
                overcurrentFault = true;

            iHs = std::clamp(iHs, -m.currentLimit, m.currentLimit);
            iLs = std::clamp(iLs, -m.currentLimit, m.currentLimit);

            i[hsOut] += iHs;
            i[0] -= iHs;

            i[lsOut] -= iLs;
            i[1] += iLs;
        }
    }

    // nFAULT open-drain active-low output: pulled low when awake & in UVLO (0.5V < vBus < minSupply) or overcurrent
    const bool faulted = (!sleepMode && vBus > 0.5 && uvlo) || overcurrentFault;
    const double rFault = faulted ? 50.0 : rHiZ;
    const double iFault = (v[7] - vGnd) / rFault;
    i[7] -= iFault;
    i[1] += iFault;

    return true;
}

int addDrv8833Driver(circuit_sim::Circuit& c, const std::string& name,
                     const std::array<circuit_sim::Node, 12>& pins,
                     const Drv8833Model& model)
{
    if (const auto p = problemsWith(model); !p.empty())
        throw std::invalid_argument(p);
    std::vector<circuit_sim::Node> nodes(pins.begin(), pins.end());
    for (int k = 8; k < 12; ++k)
        c.addResistor(name + ".bleed" + std::to_string(k), nodes[(size_t)k], pins[1], 1e9);
    return c.addProgrammable(name, nodes, std::make_shared<Drv8833Device>(model));
}

// ==============================================================================
// 2. Omron D2F-01F / D2F-01FL Subminiature Snap-Action Limit Switch (SPDT)
// ==============================================================================

std::string problemsWith(const LimitSwitchD2FModel& m)
{
    for (double v : { m.rOn, m.rOff, m.pretravelMm, m.overtravelMm, m.movementDiffMm,
                      m.operatingForceN, m.releaseForceN, m.maxVoltage, m.maxCurrent, m.travelMm })
        if (!std::isfinite(v))
            return "D2F limit switch parameters must be finite.";
    if (!(m.rOn > 0.0 && m.rOff > m.rOn))
        return "D2F limit switch contact resistances must satisfy 0 < r_on < r_off.";
    if (!(m.pretravelMm > 0.0 && m.overtravelMm > 0.0 && m.movementDiffMm >= 0.0 && m.movementDiffMm < m.pretravelMm))
        return "D2F limit switch travel parameters must satisfy 0 <= movement_differential < pretravel and overtravel > 0.";
    if (!(m.operatingForceN > 0.0 && m.releaseForceN >= 0.0 && m.releaseForceN <= m.operatingForceN))
        return "D2F limit switch force parameters must satisfy 0 <= release_force <= operating_force.";
    if (!(m.maxVoltage > 0.0 && m.maxCurrent > 0.0))
        return "D2F limit switch contact ratings must be positive.";
    return {};
}

LimitSwitchD2FDevice::LimitSwitchD2FDevice(LimitSwitchD2FModel model) : m(model)
{
    if (const auto p = problemsWith(m); !p.empty())
        throw std::invalid_argument(p);
    reset();
}

void LimitSwitchD2FDevice::reset()
{
    if (m.overrideMode == LimitSwitchD2FModel::OverrideMode::ForceActuatedNO)
        actuated = true;
    else if (m.overrideMode == LimitSwitchD2FModel::OverrideMode::ForceUnactuatedNC)
        actuated = false;
    else
        actuated = (m.travelMm >= m.pretravelMm);
    trialActuated = actuated;
}

bool LimitSwitchD2FDevice::evaluate(double, double, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    if (v.size() != 4)
    {
        error = "D2F limit switch requires 4 pins (COM, NC, NO, ACT).";
        return false;
    }

    const double totalTravelMm = m.travelMm + v[3];
    if (m.overrideMode == LimitSwitchD2FModel::OverrideMode::ForceActuatedNO)
    {
        trialActuated = true;
    }
    else if (m.overrideMode == LimitSwitchD2FModel::OverrideMode::ForceUnactuatedNC)
    {
        trialActuated = false;
    }
    else
    {
        // Snap-action hysteresis:
        // Trips from NC -> NO when travel >= PT; releases from NO -> NC when travel <= PT - MD.
        if (!actuated)
            trialActuated = (totalTravelMm >= m.pretravelMm);
        else
            trialActuated = !(totalTravelMm <= (m.pretravelMm - m.movementDiffMm));
    }

    const double rNc = trialActuated ? m.rOff : m.rOn;
    const double rNo = trialActuated ? m.rOn : m.rOff;

    const double iComNc = (v[0] - v[1]) / rNc;
    const double iComNo = (v[0] - v[2]) / rNo;

    i.assign(4, 0.0);
    i[0] = -(iComNc + iComNo);
    i[1] = iComNc;
    i[2] = iComNo;
    i[3] = -v[3] / 1e9; // High-Z mechanical actuation sense input
    return true;
}

void LimitSwitchD2FDevice::accept(double)
{
    actuated = trialActuated;
}

bool LimitSwitchD2FDevice::saveState(std::vector<std::uint8_t>& out) const
{
    out.resize(1);
    out[0] = actuated ? 1 : 0;
    return true;
}

bool LimitSwitchD2FDevice::restoreState(const std::vector<std::uint8_t>& in)
{
    if (in.size() != 1)
        return false;
    actuated = (in[0] != 0);
    trialActuated = actuated;
    return true;
}

int addLimitSwitchD2F(circuit_sim::Circuit& c, const std::string& name,
                      circuit_sim::Node com, circuit_sim::Node nc, circuit_sim::Node no, circuit_sim::Node act,
                      const LimitSwitchD2FModel& model)
{
    if (const auto p = problemsWith(model); !p.empty())
        throw std::invalid_argument(p);
    if (act != 0)
        c.addResistor(name + ".act_bleed", act, 0, 1e9);
    return c.addProgrammable(name, { com, nc, no, act }, std::make_shared<LimitSwitchD2FDevice>(model));
}

// ==============================================================================
// 3. ams AS5600 / Seeed Grove 12-Bit Contactless Magnetic Rotary Encoder
// ==============================================================================

std::string problemsWith(const As5600Model& m)
{
    for (double v : { m.minSupply, m.maxSupply, m.startAngleDeg, m.stopAngleDeg, m.angleOffsetRad, m.rOut, m.pwmFreqHz })
        if (!std::isfinite(v))
            return "AS5600 parameters must be finite.";
    if (!(m.minSupply > 0.0 && m.maxSupply > m.minSupply))
        return "AS5600 supply limits must satisfy 0 < minSupply < maxSupply.";
    if (!(m.stopAngleDeg - m.startAngleDeg >= 18.0 && m.stopAngleDeg - m.startAngleDeg <= 360.0))
        return "AS5600 angular span (stop_angle - start_angle) must be between 18 deg and 360 deg.";
    if (!(m.rOut > 0.0))
        return "AS5600 output resistance r_out must be positive.";
    if (!(m.pwmFreqHz > 0.0))
        return "AS5600 PWM frequency must be positive.";
    return {};
}

As5600EncoderDevice::As5600EncoderDevice(As5600Model model) : m(model)
{
    if (const auto p = problemsWith(m); !p.empty())
        throw std::invalid_argument(p);
}

int As5600EncoderDevice::codeForAngle(double angleRad, bool ccwPositive) const
{
    double wrapped = std::fmod(angleRad + m.angleOffsetRad, 2.0 * pi);
    if (wrapped < 0.0)
        wrapped += 2.0 * pi;
    if (ccwPositive)
    {
        wrapped = std::fmod(2.0 * pi - wrapped, 2.0 * pi);
        if (wrapped < 0.0)
            wrapped += 2.0 * pi;
    }

    const double angleDeg = wrapped * (180.0 / pi);
    const double spanDeg = m.stopAngleDeg - m.startAngleDeg;
    double relDeg = angleDeg - m.startAngleDeg;
    if (relDeg < 0.0 && m.stopAngleDeg > 360.0)
        relDeg += 360.0;

    const double norm = std::clamp(relDeg / spanDeg, 0.0, 1.0);
    const int code = static_cast<int>(std::floor(norm * 4096.0));
    return std::clamp(code, 0, 4095);
}

bool As5600EncoderDevice::evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    if (v.size() != 5)
    {
        error = "AS5600 encoder requires 5 pins (ANGLE, VCC, GND, OUT, DIR).";
        return false;
    }

    const double vGnd = v[2];
    const double vSupply = v[1] - vGnd;
    if (vSupply < -0.05)
    {
        error = "AS5600 supply polarity (VCC - GND) is reversed.";
        return false;
    }
    if (vSupply > m.maxSupply + 0.05)
    {
        error = "AS5600 supply voltage exceeds maximum rating (5.5 V).";
        return false;
    }

    i.assign(5, 0.0);

    // DIR input has a weak 1 MOhm reference pull-down to GND so unwired DIR defaults to GND (CW positive)
    const double iDir = (v[4] - vGnd) / 1e6;
    i[4] -= iDir;
    i[2] += iDir;

    if (vSupply < m.minSupply)
    {
        // Undervoltage: output is high impedance
        const double iLeak = (v[3] - vGnd) / 1e9;
        i[3] -= iLeak;
        i[2] += iLeak;
        return true;
    }

    // Active supply current (~6.5 mA typical for NOM power mode per AS5600 datasheet)
    constexpr double iQuiescent = 6.5e-3;
    i[1] -= iQuiescent;
    i[2] += iQuiescent;

    const bool ccwPositive = (v[4] - vGnd) >= 0.5 * vSupply;
    const int code = codeForAngle(v[0], ccwPositive);

    double targetFraction = 0.0;
    if (m.outputMode == As5600Model::OutputMode::AnalogFullRange)
    {
        targetFraction = static_cast<double>(code) / 4095.0;
    }
    else if (m.outputMode == As5600Model::OutputMode::AnalogReduced10To90)
    {
        targetFraction = 0.10 + 0.80 * (static_cast<double>(code) / 4095.0);
    }
    else
    {
        // PWM mode: 128 init high clocks + (code) high clocks + (4095 - code) low clocks + 128 exit low clocks = 4351 total clocks
        const double duty = static_cast<double>(128 + code) / 4351.0;
        const double period = 1.0 / m.pwmFreqHz;
        double phase = std::fmod(t, period);
        if (phase < 0.0)
            phase += period;
        targetFraction = (phase < duty * period) ? 1.0 : 0.0;
    }

    const double vTarget = vGnd + targetFraction * vSupply;
    const double iDrive = (vTarget - v[3]) / m.rOut;
    i[3] += iDrive;
    if (iDrive >= 0.0)
        i[1] -= iDrive;
    else
        i[2] -= iDrive;

    return true;
}

int addAs5600Encoder(circuit_sim::Circuit& c, const std::string& name,
                     circuit_sim::Node angleNode, circuit_sim::Node vcc, circuit_sim::Node gnd,
                     circuit_sim::Node outNode, circuit_sim::Node dirNode,
                     const As5600Model& model)
{
    if (const auto p = problemsWith(model); !p.empty())
        throw std::invalid_argument(p);
    c.addResistor(name + ".out_bleed", outNode, gnd, 1e9);
    if (angleNode != 0)
        c.addResistor(name + ".angle_bleed", angleNode, 0, 1e9);
    return c.addProgrammable(name, { angleNode, vcc, gnd, outNode, dirNode }, std::make_shared<As5600EncoderDevice>(model));
}

// ==============================================================================
// 4a. Positional RC Servo (TowerPro SG90, MG90S)
// ==============================================================================

std::string problemsWith(const PositionalServoModel& m, const MotorLoad& load)
{
    for (double v : { m.minSupply, m.maxSupply, m.unloadedSpeedDegPerSec4V8, m.stallTorqueKgCm4V8,
                      m.minPulseUs, m.neutralPulseUs, m.maxPulseUs, m.deadbandUs, m.maxAngleDeg,
                      m.commandPulseUs, m.kpRadPerSecPerRad, m.idleCurrentA, m.stallCurrentA,
                      m.outputInertia, m.outputDamping, load.inertia, load.damping })
        if (!std::isfinite(v))
            return "Positional servo parameters must be finite.";
    if (!(m.minSupply > 0.0 && m.maxSupply > m.minSupply))
        return "Positional servo supply range must satisfy 0 < minSupply < maxSupply.";
    if (!(m.unloadedSpeedDegPerSec4V8 > 0.0 && m.stallTorqueKgCm4V8 > 0.0))
        return "Positional servo unloaded speed and stall torque must be positive.";
    if (!(m.minPulseUs > 0.0 && m.neutralPulseUs > m.minPulseUs && m.maxPulseUs > m.neutralPulseUs))
        return "Positional servo pulse limits must satisfy 0 < min_pulse < neutral_pulse < max_pulse.";
    if (!(m.deadbandUs >= 0.0 && m.maxAngleDeg > 0.0 && m.kpRadPerSecPerRad > 0.0))
        return "Positional servo deadband, max angle, and loop gain must be valid.";
    if (!(m.idleCurrentA >= 0.0 && m.stallCurrentA > m.idleCurrentA))
        return "Positional servo stall current must exceed idle current.";
    if (!(m.outputInertia > 0.0 && m.outputDamping >= 0.0 && load.inertia >= 0.0 && load.damping >= 0.0))
        return "Positional servo inertia must be positive and damping nonnegative.";
    return {};
}

PositionalServoDevice::PositionalServoDevice(PositionalServoModel model) : m(model)
{
    if (const auto p = problemsWith(m, {}); !p.empty())
        throw std::invalid_argument(p);
    reset();
}

void PositionalServoDevice::reset()
{
    state = { 0.0, -1.0, m.commandPulseUs, m.commandPulseUs };
    trial = state;
}

double PositionalServoDevice::targetAngleRad(double pulseUs) const
{
    const double clampedPulse = std::clamp(pulseUs, m.minPulseUs, m.maxPulseUs);
    const double maxRad = m.maxAngleDeg * (pi / 180.0);
    if (clampedPulse >= m.neutralPulseUs)
    {
        const double span = std::max(m.maxPulseUs - m.neutralPulseUs, 1e-6);
        return maxRad * ((clampedPulse - m.neutralPulseUs) / span);
    }
    const double span = std::max(m.neutralPulseUs - m.minPulseUs, 1e-6);
    return -maxRad * ((m.neutralPulseUs - clampedPulse) / span);
}

bool PositionalServoDevice::evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    if (v.size() != 6)
    {
        error = "Positional servo requires 6 pins (VCC, GND, PWM, shaft, angle, torque).";
        return false;
    }

    const double vGnd = v[1];
    const double vSupply = v[0] - vGnd;
    if (vSupply < -0.05)
    {
        error = "Servo supply polarity (VCC - GND) is reversed.";
        return false;
    }
    if (vSupply > m.maxSupply + 0.05)
    {
        error = "Servo supply voltage exceeds maximum rating (6.5 V).";
        return false;
    }

    trial = state;
    const double vPwm = v[2] - vGnd;
    if (h > 0.0)
    {
        const bool pwmHigh = (vPwm >= 1.5);
        const bool wasHigh = (state[0] != 0.0);
        if (pwmHigh && !wasHigh)
        {
            trial[0] = 1.0;
            trial[1] = t;
        }
        else if (!pwmHigh && wasHigh)
        {
            trial[0] = 0.0;
            if (state[1] >= 0.0)
            {
                const double widthUs = (t - state[1]) * 1e6;
                if (widthUs >= 300.0 && widthUs <= 2700.0)
                    trial[2] = widthUs;
            }
        }
        // Apply deadband hysteresis on pulse command
        if (std::abs(trial[2] - state[3]) >= 0.5 * m.deadbandUs)
            trial[3] = trial[2];
    }

    i.assign(6, 0.0);
    // Input impedance on PWM signal line (100 kOhm to GND)
    const double iPwm = vPwm / 100e3;
    i[2] -= iPwm;
    i[1] += iPwm;

    if (vSupply < m.minSupply)
    {
        i[5] = -v[5] / 1e-4;
        return true;
    }

    // Scale unloaded speed and stall torque with supply voltage relative to 4.8 V datasheet rating
    const double vScale = std::clamp(vSupply / 4.8, 0.7, 1.35);
    const double omegaMax = (m.unloadedSpeedDegPerSec4V8 * (pi / 180.0)) * vScale;
    const double tStall = (m.stallTorqueKgCm4V8 * robotics_catalog::kgCmToNm) * vScale;

    const double thetaTarget = targetAngleRad(trial[3]);
    const double omega = v[3];
    const double theta = v[4];
    const double posErr = thetaTarget - theta;

    // Rate-limited position loop: slews at omegaMax for large steps, linear near target
    const double omegaCmd = omegaMax * std::tanh((m.kpRadPerSecPerRad * posErr) / std::max(omegaMax, 1e-6));
    const double velGain = 25.0 * (tStall / std::max(omegaMax, 1e-6));
    const double tDrive = std::clamp(velGain * (omegaCmd - omega), -tStall, tStall);

    i[3] = tDrive;
    i[5] = (tDrive - v[5]) / 1e-4;

    const double loadFraction = std::clamp(std::abs(tDrive) / std::max(tStall, 1e-9), 0.0, 1.0);
    const double iSupply = m.idleCurrentA + (m.stallCurrentA * vScale - m.idleCurrentA) * loadFraction;
    i[0] -= iSupply;
    i[1] += iSupply;

    return true;
}

void PositionalServoDevice::accept(double)
{
    state = trial;
}

bool PositionalServoDevice::saveState(std::vector<std::uint8_t>& out) const
{
    out.resize(sizeof(state));
    std::memcpy(out.data(), state.data(), sizeof(state));
    return true;
}

bool PositionalServoDevice::restoreState(const std::vector<std::uint8_t>& in)
{
    if (in.size() != sizeof(state))
        return false;
    std::memcpy(state.data(), in.data(), sizeof(state));
    trial = state;
    return true;
}

ServoActuator addPositionalServo(circuit_sim::Circuit& c, const std::string& name,
                                 circuit_sim::Node vcc, circuit_sim::Node gnd, circuit_sim::Node pwm,
                                 const PositionalServoModel& m, const MotorLoad& load, const ShaftModel& initial)
{
    if (const auto p = problemsWith(m, load); !p.empty())
        throw std::invalid_argument(p);
    if (const auto sp = problemsWith(initial); !sp.empty())
        throw std::invalid_argument(sp);

    ServoActuator out;
    out.shaft = c.addQuantityNode();
    out.angle = c.addQuantityNode();
    out.torque = c.addQuantityNode();

    const int inertia = c.addCapacitor(name + ".inertia", out.shaft, 0, m.outputInertia + load.inertia);
    auto& cap = c.elements()[(size_t)inertia];
    cap.hasInitialCondition = true;
    cap.initialCondition = initial.omega0;
    cap.holdInitialCondition = true;

    if (m.outputDamping + load.damping > 0.0)
        c.addResistor(name + ".damping", out.shaft, 0, 1.0 / (m.outputDamping + load.damping));

    c.addCurrentSource(name + ".load", out.shaft, 0, load.torque);
    c.addProgrammable(name + ".shaft", { out.shaft, out.angle }, std::make_shared<ShaftDevice>(initial));
    out.device = c.addProgrammable(name, { vcc, gnd, pwm, out.shaft, out.angle, out.torque },
                                   std::make_shared<PositionalServoDevice>(m));

    const auto addQ = [&](const std::string& key, const std::string& unit, std::vector<Factor> f, double gain = 1.0) {
        Quantity q;
        q.name = name + "." + key;
        q.unit = unit;
        q.description = "Positional servo quantity: " + key;
        q.factors = std::move(f);
        q.gain = gain;
        out.quantities.push_back(q);
    };
    addQ("speed", "rad/s", { voltage(out.shaft) });
    addQ("rpm", "rpm", { voltage(out.shaft) }, 60.0 / (2.0 * pi));
    addQ("position", "rad", { voltage(out.angle) });
    addQ("position_deg", "deg", { voltage(out.angle) }, 180.0 / pi);
    addQ("torque", "N*m", { voltage(out.torque) });
    return out;
}

// ==============================================================================
// 4b. Continuous-Rotation RC Servo (FEETECH FS90R V1.0 / V2.0)
// ==============================================================================

std::string problemsWith(const ContinuousServoModel& m, const MotorLoad& load)
{
    for (double v : { m.minSupply, m.maxSupply, m.noLoadRpm4V8, m.noLoadRpm6V,
                      m.noLoadCurrent4V8, m.noLoadCurrent6V, m.stallTorqueKgCm4V8, m.stallTorqueKgCm6V,
                      m.stallCurrent4V8, m.stallCurrent6V, m.idleCurrentA,
                      m.minPulseUs, m.neutralPulseUs, m.maxPulseUs, m.deadbandUs, m.commandPulseUs,
                      m.outputInertia, m.outputDamping, load.inertia, load.damping })
        if (!std::isfinite(v))
            return "Continuous servo parameters must be finite.";
    if (!(m.minSupply > 0.0 && m.maxSupply > m.minSupply))
        return "Continuous servo supply limits must satisfy 0 < minSupply < maxSupply.";
    if (!(m.noLoadRpm4V8 > 0.0 && m.noLoadRpm6V > 0.0 && m.stallTorqueKgCm4V8 > 0.0 && m.stallTorqueKgCm6V > 0.0))
        return "Continuous servo speed and stall torque ratings must be positive.";
    if (!(m.minPulseUs > 0.0 && m.neutralPulseUs > m.minPulseUs && m.maxPulseUs > m.neutralPulseUs))
        return "Continuous servo pulse limits must satisfy 0 < min_pulse < neutral_pulse < max_pulse.";
    if (!(m.deadbandUs >= 0.0 && m.deadbandUs < (m.maxPulseUs - m.minPulseUs)))
        return "Continuous servo deadband must be nonnegative and smaller than total pulse range.";
    if (!(m.outputInertia > 0.0 && m.outputDamping >= 0.0 && load.inertia >= 0.0 && load.damping >= 0.0))
        return "Continuous servo inertia must be positive and damping nonnegative.";
    return {};
}

ContinuousServoDevice::ContinuousServoDevice(ContinuousServoModel model) : m(model)
{
    if (const auto p = problemsWith(m, {}); !p.empty())
        throw std::invalid_argument(p);
    reset();
}

void ContinuousServoDevice::reset()
{
    state = { 0.0, -1.0, m.commandPulseUs };
    trial = state;
}

double ContinuousServoDevice::normalizedThrottle(double pulseUs) const
{
    const double halfDb = 0.5 * m.deadbandUs;
    const double diff = pulseUs - m.neutralPulseUs;
    if (std::abs(diff) <= halfDb)
        return 0.0;
    if (diff > halfDb)
    {
        const double span = std::max(m.maxPulseUs - (m.neutralPulseUs + halfDb), 1e-6);
        return std::clamp((diff - halfDb) / span, 0.0, 1.0);
    }
    const double span = std::max((m.neutralPulseUs - halfDb) - m.minPulseUs, 1e-6);
    return -std::clamp((-diff - halfDb) / span, 0.0, 1.0);
}

bool ContinuousServoDevice::evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    if (v.size() != 6)
    {
        error = "Continuous servo requires 6 pins (VCC, GND, PWM, shaft, angle, torque).";
        return false;
    }

    const double vGnd = v[1];
    const double vSupply = v[0] - vGnd;
    if (vSupply < -0.05)
    {
        error = "Servo supply polarity (VCC - GND) is reversed.";
        return false;
    }
    if (vSupply > m.maxSupply + 0.05)
    {
        error = "Servo supply voltage exceeds maximum rating (6.5 V).";
        return false;
    }

    trial = state;
    const double vPwm = v[2] - vGnd;
    if (h > 0.0)
    {
        const bool pwmHigh = (vPwm >= 1.5);
        const bool wasHigh = (state[0] != 0.0);
        if (pwmHigh && !wasHigh)
        {
            trial[0] = 1.0;
            trial[1] = t;
        }
        else if (!pwmHigh && wasHigh)
        {
            trial[0] = 0.0;
            if (state[1] >= 0.0)
            {
                const double widthUs = (t - state[1]) * 1e6;
                if (widthUs >= 300.0 && widthUs <= 2700.0)
                    trial[2] = widthUs;
            }
        }
    }

    i.assign(6, 0.0);
    const double iPwm = vPwm / 100e3;
    i[2] -= iPwm;
    i[1] += iPwm;

    if (vSupply < m.minSupply)
    {
        i[5] = -v[5] / 1e-4;
        return true;
    }

    // Interpolate 4.8 V .. 6.0 V datasheet endpoints
    const double alpha = std::clamp((vSupply - 4.8) / (6.0 - 4.8), 0.0, 1.0);
    const double scaleBelow4V8 = (vSupply < 4.8) ? (vSupply / 4.8) : 1.0;
    const double noLoadRpm = (m.noLoadRpm4V8 + alpha * (m.noLoadRpm6V - m.noLoadRpm4V8)) * scaleBelow4V8;
    const double omegaNoLoad = std::max(noLoadRpm * (2.0 * pi / 60.0), 1e-6);
    const double tStall = ((m.stallTorqueKgCm4V8 + alpha * (m.stallTorqueKgCm6V - m.stallTorqueKgCm4V8))
                           * robotics_catalog::kgCmToNm) * scaleBelow4V8;
    const double iNoLoad = (m.noLoadCurrent4V8 + alpha * (m.noLoadCurrent6V - m.noLoadCurrent4V8)) * scaleBelow4V8;
    const double iStall = (m.stallCurrent4V8 + alpha * (m.stallCurrent6V - m.stallCurrent4V8)) * scaleBelow4V8;

    const double u = normalizedThrottle(trial[2]);
    const double omega = v[3];

    double tDrive = 0.0;
    double iSupply = m.idleCurrentA;
    if (std::abs(u) > 0.0)
    {
        tDrive = tStall * (u - (omega / omegaNoLoad));
        tDrive = std::clamp(tDrive, -tStall, tStall);
        const double loadFrac = std::clamp(std::abs(tDrive) / std::max(tStall, 1e-9), 0.0, 1.0);
        iSupply = std::abs(u) * iNoLoad + (iStall - std::abs(u) * iNoLoad) * loadFrac;
    }
    else
    {
        // Inside neutral deadband: dynamic braking damping holds shaft at 0 rad/s
        tDrive = -(tStall / omegaNoLoad) * omega;
        iSupply = m.idleCurrentA;
    }

    i[3] = tDrive;
    i[5] = (tDrive - v[5]) / 1e-4;
    i[0] -= iSupply;
    i[1] += iSupply;
    return true;
}

void ContinuousServoDevice::accept(double)
{
    state = trial;
}

bool ContinuousServoDevice::saveState(std::vector<std::uint8_t>& out) const
{
    out.resize(sizeof(state));
    std::memcpy(out.data(), state.data(), sizeof(state));
    return true;
}

bool ContinuousServoDevice::restoreState(const std::vector<std::uint8_t>& in)
{
    if (in.size() != sizeof(state))
        return false;
    std::memcpy(state.data(), in.data(), sizeof(state));
    trial = state;
    return true;
}

ServoActuator addContinuousServo(circuit_sim::Circuit& c, const std::string& name,
                                 circuit_sim::Node vcc, circuit_sim::Node gnd, circuit_sim::Node pwm,
                                 const ContinuousServoModel& m, const MotorLoad& load, const ShaftModel& initial)
{
    if (const auto p = problemsWith(m, load); !p.empty())
        throw std::invalid_argument(p);
    if (const auto sp = problemsWith(initial); !sp.empty())
        throw std::invalid_argument(sp);

    ServoActuator out;
    out.shaft = c.addQuantityNode();
    out.angle = c.addQuantityNode();
    out.torque = c.addQuantityNode();

    const int inertia = c.addCapacitor(name + ".inertia", out.shaft, 0, m.outputInertia + load.inertia);
    auto& cap = c.elements()[(size_t)inertia];
    cap.hasInitialCondition = true;
    cap.initialCondition = initial.omega0;
    cap.holdInitialCondition = true;

    if (m.outputDamping + load.damping > 0.0)
        c.addResistor(name + ".damping", out.shaft, 0, 1.0 / (m.outputDamping + load.damping));

    c.addCurrentSource(name + ".load", out.shaft, 0, load.torque);
    c.addProgrammable(name + ".shaft", { out.shaft, out.angle }, std::make_shared<ShaftDevice>(initial));
    out.device = c.addProgrammable(name, { vcc, gnd, pwm, out.shaft, out.angle, out.torque },
                                   std::make_shared<ContinuousServoDevice>(m));

    const auto addQ = [&](const std::string& key, const std::string& unit, std::vector<Factor> f, double gain = 1.0) {
        Quantity q;
        q.name = name + "." + key;
        q.unit = unit;
        q.description = "Continuous servo quantity: " + key;
        q.factors = std::move(f);
        q.gain = gain;
        out.quantities.push_back(q);
    };
    addQ("speed", "rad/s", { voltage(out.shaft) });
    addQ("rpm", "rpm", { voltage(out.shaft) }, 60.0 / (2.0 * pi));
    addQ("position", "rad", { voltage(out.angle) });
    addQ("torque", "N*m", { voltage(out.torque) });
    return out;
}

} // namespace mech

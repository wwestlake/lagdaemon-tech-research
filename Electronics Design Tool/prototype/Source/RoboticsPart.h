#pragma once

#include "RoboticsComponents.h"
#include <functional>

namespace robotics_catalog
{
struct BuildResult
{
    int element = -1;
    circuit_sim::Node speed = 0;
    circuit_sim::Node angle = 0;
    std::vector<mech::Quantity> quantities;
    juce::String error;
    juce::String warning;
};

inline BuildResult build(circuit_sim::Circuit& c,
                         const Reference& reference,
                         const std::string& name,
                         const std::function<circuit_sim::Node(const char*)>& pin,
                         const std::function<juce::String(const char*)>& raw,
                         circuit_sim::Node targetMotorAngle = 0)
{
    BuildResult out;
    const auto number = [&](const char* key, double fallback) {
        const auto text = raw(key).trim();
        double value = fallback;
        if (text.isNotEmpty() && (!circuit_sim::parseValue(text.toStdString(), value) || !std::isfinite(value)))
            out.error = "Invalid robotics component parameter: " + juce::String(key);
        return value;
    };

    try
    {
        switch (reference.family)
        {
            case Family::HBridgeDrv8833:
            {
                mech::Drv8833Model m;
                m.minSupply = number("min_supply", reference.p0);
                m.maxSupply = number("max_supply", reference.p1);
                m.rdsOn = number("rds_on", reference.p2);
                m.currentLimit = number("current_limit", reference.p3);
                m.logicHigh = number("logic_high", 2.0);
                m.logicLow = number("logic_low", 0.8);
                m.carrierPullupSleep = (reference.p5 != 0.0);
                if (const auto p = mech::problemsWith(m); !p.empty())
                    out.error = p;
                if (out.error.isNotEmpty())
                    return out;

                std::array<circuit_sim::Node, 12> pins {{
                    pin("VM"), pin("GND"),
                    pin("AIN1"), pin("AIN2"),
                    pin("BIN1"), pin("BIN2"),
                    pin("nSLEEP"), pin("nFAULT"),
                    pin("AOUT1"), pin("AOUT2"),
                    pin("BOUT1"), pin("BOUT2")
                }};
                out.element = mech::addDrv8833Driver(c, name, pins, m);
                out.warning = m.carrierPullupSleep
                    ? "Pololu 2130 DRV8833 carrier: dual H-bridge (Rds_on=0.36 ohm typ, 1.2 A continuous / 2 A peak limit, 47 kOhm nSLEEP pull-up to VM). Averaged current limiting; no thermal/PWM switching ripple model."
                    : "TI DRV8833PWPR dual H-bridge: Rds_on=0.36 ohm typ, 1.5 A RMS / 2 A peak limit, internal nSLEEP pull-down (drive nSLEEP >= 2 V to enable). Averaged current limiting; no thermal/PWM switching ripple model.";
                break;
            }
            case Family::LimitSwitchD2F:
            {
                mech::LimitSwitchD2FModel m;
                m.maxVoltage = number("max_voltage", reference.p0);
                m.maxCurrent = number("max_current", reference.p1);
                m.rOn = number("ron", reference.p2);
                m.rOff = number("roff", 1e9);
                m.operatingForceN = number("operating_force", reference.p3);
                m.releaseForceN = number("release_force", reference.p4);
                m.pretravelMm = number("pretravel_mm", reference.p5);
                m.overtravelMm = number("overtravel_mm", reference.p6);
                m.movementDiffMm = number("movement_diff_mm", reference.p7);
                m.travelMm = number("travel_mm", 0.0);

                const auto modeStr = raw("state_override").trim();
                if (modeStr == "Unactuated (NC)")
                    m.overrideMode = mech::LimitSwitchD2FModel::OverrideMode::ForceUnactuatedNC;
                else if (modeStr == "Actuated (NO)")
                    m.overrideMode = mech::LimitSwitchD2FModel::OverrideMode::ForceActuatedNO;
                else
                    m.overrideMode = mech::LimitSwitchD2FModel::OverrideMode::AutoFromTravel;

                if (const auto p = mech::problemsWith(m); !p.empty())
                    out.error = p;
                if (out.error.isNotEmpty())
                    return out;

                out.element = mech::addLimitSwitchD2F(c, name, pin("COM"), pin("NC"), pin("NO"), pin("ACT"), m);
                out.warning = juce::String(reference.part)
                    + ": SPDT snap-action microswitch (R_on <= 0.1 ohm, PT="
                    + juce::String(m.pretravelMm, 2) + " mm, MD=" + juce::String(m.movementDiffMm, 2)
                    + " mm, 30 VDC / 0.1 A resistive rating). Contact bounce and mechanical force-displacement stiffness are not modeled.";
                break;
            }
            case Family::MagneticEncoderAs5600:
            {
                mech::As5600Model m;
                m.minSupply = number("min_supply", reference.p0);
                m.maxSupply = number("max_supply", reference.p1);
                m.rOut = number("rout", reference.p3);
                m.startAngleDeg = number("start_angle_deg", 0.0);
                m.stopAngleDeg = number("stop_angle_deg", 360.0);
                m.angleOffsetRad = number("angle_offset_rad", 0.0);
                m.pwmFreqHz = number("pwm_frequency", 460.0);

                const auto modeStr = raw("output_mode").trim();
                if (modeStr == "Analog (10%-90% VCC)")
                    m.outputMode = mech::As5600Model::OutputMode::AnalogReduced10To90;
                else if (modeStr == "PWM")
                    m.outputMode = mech::As5600Model::OutputMode::PwmOutput;
                else
                    m.outputMode = mech::As5600Model::OutputMode::AnalogFullRange;

                if (const auto p = mech::problemsWith(m); !p.empty())
                    out.error = p;
                if (out.error.isNotEmpty())
                    return out;

                auto angleNode = pin("ANGLE");
                if (angleNode == 0 && targetMotorAngle != 0)
                    angleNode = targetMotorAngle;

                out.element = mech::addAs5600Encoder(c, name, angleNode, pin("VCC"), pin("GND"), pin("OUT"), pin("DIR"), m);
                out.warning = juce::String(reference.part)
                    + ": 12-bit (4096-step) contactless magnetic rotary position sensor (3.0-5.5 V). Ratiometric analog/PWM output; I2C register programming and airgap field strength limits are not simulated.";
                break;
            }
            case Family::PositionalServo:
            {
                mech::PositionalServoModel m;
                m.unloadedSpeedDegPerSec4V8 = number("unloaded_speed_deg_s", reference.p2);
                m.stallTorqueKgCm4V8 = number("stall_torque_kgcm", reference.p3);
                m.minPulseUs = number("min_pulse_us", reference.p4);
                m.neutralPulseUs = number("neutral_pulse_us", reference.p5);
                m.maxPulseUs = number("max_pulse_us", reference.p6);
                m.deadbandUs = number("deadband_us", reference.p7);
                m.idleCurrentA = number("idle_current", reference.p8);
                m.stallCurrentA = number("stall_current", reference.p9);
                m.maxAngleDeg = number("max_angle_deg", 90.0);
                m.commandPulseUs = number("command_pulse_us", m.neutralPulseUs);
                m.kpRadPerSecPerRad = number("loop_gain", 60.0);
                m.outputInertia = number("j", 1e-5);
                m.outputDamping = number("b", 0.0);

                mech::MotorLoad load;
                load.inertia = number("load_inertia", 0.0);
                load.damping = number("load_damping", 0.0);
                load.torque.offset = number("load_torque", 0.0);

                mech::ShaftModel initial;
                initial.theta0 = number("initial_position", 0.0);
                initial.omega0 = number("initial_speed", 0.0);

                if (const auto p = mech::problemsWith(m, load); !p.empty())
                    out.error = p;
                if (const auto sp = mech::problemsWith(initial); !sp.empty())
                    out.error = sp;
                if (out.error.isNotEmpty())
                    return out;

                const auto servo = mech::addPositionalServo(c, name, pin("VCC"), pin("GND"), pin("PWM"), m, load, initial);
                out.element = servo.device;
                out.speed = servo.shaft;
                out.angle = servo.angle;
                out.quantities = servo.quantities;
                out.warning = juce::String(reference.part)
                    + ": Behavioral RC positional servo calibrated to published unloaded speed ("
                    + juce::String(m.unloadedSpeedDegPerSec4V8, 0) + " deg/s @ 4.8 V) and stall torque ("
                    + juce::String(m.stallTorqueKgCm4V8, 2) + " kgf*cm @ 4.8 V). Internal control loop gain and inertia are reference assumptions.";
                break;
            }
            case Family::ContinuousServo:
            {
                mech::ContinuousServoModel m;
                m.noLoadRpm4V8 = number("noload_rpm_4v8", reference.p2);
                m.noLoadRpm6V = number("noload_rpm_6v", reference.p3);
                m.noLoadCurrent4V8 = number("noload_current_4v8", reference.p4);
                m.noLoadCurrent6V = number("noload_current_6v", reference.p5);
                m.stallTorqueKgCm4V8 = number("stall_torque_kgcm_4v8", reference.p6);
                m.stallTorqueKgCm6V = number("stall_torque_kgcm_6v", reference.p7);
                m.stallCurrent4V8 = number("stall_current_4v8", reference.p8);
                m.stallCurrent6V = number("stall_current_6v", reference.p9);
                m.minPulseUs = number("min_pulse_us", reference.p10);
                m.neutralPulseUs = number("neutral_pulse_us", reference.p11);
                m.maxPulseUs = number("max_pulse_us", reference.p12);
                m.deadbandUs = number("deadband_us", 90.0);
                m.idleCurrentA = number("idle_current", 0.005);
                m.commandPulseUs = number("command_pulse_us", m.neutralPulseUs);
                m.outputInertia = number("j", 1e-5);
                m.outputDamping = number("b", 0.0);

                mech::MotorLoad load;
                load.inertia = number("load_inertia", 0.0);
                load.damping = number("load_damping", 0.0);
                load.torque.offset = number("load_torque", 0.0);

                mech::ShaftModel initial;
                initial.theta0 = number("initial_position", 0.0);
                initial.omega0 = number("initial_speed", 0.0);

                if (const auto p = mech::problemsWith(m, load); !p.empty())
                    out.error = p;
                if (const auto sp = mech::problemsWith(initial); !sp.empty())
                    out.error = sp;
                if (out.error.isNotEmpty())
                    return out;

                const auto servo = mech::addContinuousServo(c, name, pin("VCC"), pin("GND"), pin("PWM"), m, load, initial);
                out.element = servo.device;
                out.speed = servo.shaft;
                out.angle = servo.angle;
                out.quantities = servo.quantities;
                out.warning = juce::String(reference.part)
                    + ": Behavioral RC continuous-rotation servo calibrated to FEETECH 4.8 V / 6.0 V datasheet endpoints (80/100 rpm no-load, 1.3/1.5 kgf*cm stall, 90 us deadband around 1500 us). Output inertia is a reference assumption.";
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        out.error = e.what();
    }

    return out;
}
} // namespace robotics_catalog

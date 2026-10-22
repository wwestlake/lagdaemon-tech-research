#pragma once

#include <array>
#include <string>

// Manufacturer-backed robotics component references matching
// assets/robotics-component-library.json and DjehutiCAD canonical IDs.
namespace robotics_catalog
{
enum class Family
{
    HBridgeDrv8833,
    LimitSwitchD2F,
    MagneticEncoderAs5600,
    PositionalServo,
    ContinuousServo
};

struct Reference
{
    const char* symbol;
    const char* id;
    const char* part;
    const char* name;
    const char* source;
    Family family;

    // Family-specific published typical/limit specifications:
    // HBridgeDrv8833: minSupply [V], maxSupply [V], rdsOn [ohm], continuousCurrent [A], peakCurrent [A], carrierPullup (1=47k to VM, 0=500k pulldown)
    // LimitSwitchD2F: maxVoltage [V], maxCurrent [A], rOn [ohm], operatingForce [N], releaseForce [N], pretravelMm [mm], overtravelMm [mm], movementDiffMm [mm]
    // MagneticEncoderAs5600: minSupply [V], maxSupply [V], resolutionBits [12], defaultRout [ohm]
    // PositionalServo: minSupply [V], maxSupply [V], unloadedSpeedDegPerSec4V8 [deg/s], stallTorqueKgCm4V8 [kg*cm], minPulseUs [us], neutralPulseUs [us], maxPulseUs [us], deadbandUs [us], idleCurrentA [A], stallCurrentA [A]
    // ContinuousServo: minSupply [V], maxSupply [V], noLoadRpm4V8 [rpm], noLoadRpm6V [rpm], noLoadCurrent4V8 [A], noLoadCurrent6V [A], stallTorqueKgCm4V8 [kg*cm], stallTorqueKgCm6V [kg*cm], stallCurrent4V8 [A], stallCurrent6V [A], minPulseUs [us], neutralPulseUs [us], maxPulseUs [us], deadbandUs [us], idleCurrent4V8 [A]
    double p0 = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    double p3 = 0.0;
    double p4 = 0.0;
    double p5 = 0.0;
    double p6 = 0.0;
    double p7 = 0.0;
    double p8 = 0.0;
    double p9 = 0.0;
    double p10 = 0.0;
    double p11 = 0.0;
    double p12 = 0.0;
};

inline const std::array<Reference, 10>& references()
{
    static const std::array<Reference, 10> data {{
        // 1. DRV8833 dual H-bridge driver family (TI SLVSAR1E & Pololu #2130 carrier)
        {
            "driver_ti_drv8833pwpr",
            "driver-ti-drv8833pwpr",
            "DRV8833PWPR",
            "TI DRV8833PWPR: Dual H-Bridge Motor Driver (2.7-10.8 V, 1.5 A RMS)",
            "assets/manufacturer-foundation/drv8833.pdf",
            Family::HBridgeDrv8833,
            2.7, 10.8, 0.36, 1.5, 2.0, 0.0
        },
        {
            "driver_pololu_2130",
            "driver-pololu-2130",
            "2130",
            "Pololu 2130: DRV8833 Dual Motor Driver Carrier (2.7-10.8 V, 1.2 A)",
            "assets/manufacturer-foundation/drv8833-dual-motor-driver-carrier-schematic.pdf",
            Family::HBridgeDrv8833,
            2.7, 10.8, 0.36, 1.2, 2.0, 1.0
        },

        // 2. Omron D2F subminiature snap-action limit switch family (en-d2f.pdf)
        {
            "switch_omron_d2f_01f",
            "switch-omron-d2f-01f",
            "D2F-01F",
            "Omron D2F-01F: Subminiature Microswitch (Pin Plunger, 30 VDC 0.1 A)",
            "assets/manufacturer-foundation/en-d2f.pdf",
            Family::LimitSwitchD2F,
            30.0, 0.1, 0.1, 0.74, 0.15, 0.5, 0.25, 0.1
        },
        {
            "switch_omron_d2f_01fl",
            "switch-omron-d2f-01fl",
            "D2F-01FL",
            "Omron D2F-01FL: Subminiature Microswitch (Hinge Lever, 30 VDC 0.1 A)",
            "assets/manufacturer-foundation/en-d2f.pdf",
            Family::LimitSwitchD2F,
            30.0, 0.1, 0.1, 0.25, 0.03, 2.5, 0.55, 0.5
        },

        // 3. ams AS5600 12-bit contactless magnetic rotary encoder family
        {
            "sensor_ams_as5600",
            "sensor-ams-as5600",
            "AS5600",
            "ams AS5600: 12-Bit Programmable Contactless Magnetic Rotary Position Sensor",
            "assets/manufacturer-foundation/AS5600_DS000365_5-00.pdf",
            Family::MagneticEncoderAs5600,
            3.0, 5.5, 4096.0, 100.0
        },
        {
            "module_seeed_grove_as5600",
            "module-seeed-grove-as5600",
            "101020692",
            "Seeed Grove 101020692: 12-Bit Magnetic Rotary Position Sensor (AS5600)",
            "assets/manufacturer-foundation/Grove-12-bit-Magnetic-Rotary-Position-Sensor-AS5600_sch.pdf",
            Family::MagneticEncoderAs5600,
            3.0, 5.5, 4096.0, 100.0
        },

        // 4. Positional and continuous-rotation RC servos
        {
            "servo_towerpro_sg90",
            "servo-positional-towerpro-sg90",
            "SG90",
            "TowerPro SG90: 9g Micro Positional Servo (4.8 V, 1.8 kgf*cm)",
            "assets/manufacturer-foundation/SG90_TowerPro.pdf",
            Family::PositionalServo,
            4.8, 6.0, 500.0, 1.8, 500.0, 1500.0, 2400.0, 1.0, 0.010, 0.650
        },
        {
            "servo_towerpro_mg90s",
            "servo-positional-towerpro-mg90s",
            "MG90S",
            "TowerPro MG90S: Metal-Gear Micro Positional Servo (4.8-6.0 V, 1.8-2.2 kgf*cm)",
            "assets/manufacturer-foundation/MG90S_TowerPro.pdf",
            Family::PositionalServo,
            4.8, 6.0, 600.0, 1.8, 500.0, 1500.0, 2400.0, 1.0, 0.010, 0.750
        },
        {
            "servo_feetech_fs90r_v1",
            "servo-continuous-feetech-fs90r-v1",
            "FS90R",
            "FEETECH FS90R (V1.0): Micro Continuous-Rotation Servo (4.8-6.0 V)",
            "assets/manufacturer-foundation/FS90R V1.0.pdf",
            Family::ContinuousServo,
            4.8, 6.0, 80.0, 100.0, 0.070, 0.080, 1.3, 1.5, 0.700, 0.800, 700.0, 1500.0, 2300.0
        },
        {
            "servo_feetech_fs90r_v2",
            "servo-continuous-feetech-fs90r-v2",
            "FS90R",
            "FEETECH FS90R (V2.0): Micro Continuous-Rotation Servo (4.8-6.0 V)",
            "assets/manufacturer-foundation/FS90R V2.0.pdf",
            Family::ContinuousServo,
            4.8, 6.0, 80.0, 100.0, 0.070, 0.080, 1.3, 1.5, 0.700, 0.800, 700.0, 1500.0, 2300.0
        }
    }};
    return data;
}

inline const Reference* find(const std::string& symbol)
{
    for (const auto& r : references())
        if (symbol == r.symbol)
            return &r;
    return nullptr;
}

inline bool isRoboticsPart(const std::string& symbol)
{
    return find(symbol) != nullptr;
}

inline bool isHBridge(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && r->family == Family::HBridgeDrv8833;
}

inline bool isLimitSwitch(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && r->family == Family::LimitSwitchD2F;
}

inline bool isMagneticEncoder(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && r->family == Family::MagneticEncoderAs5600;
}

inline bool isServo(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && (r->family == Family::PositionalServo || r->family == Family::ContinuousServo);
}

inline bool isPositionalServo(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && r->family == Family::PositionalServo;
}

inline bool isContinuousServo(const std::string& symbol)
{
    const auto* r = find(symbol);
    return r != nullptr && r->family == Family::ContinuousServo;
}

constexpr double kgCmToNm = 0.0980665;
} // namespace robotics_catalog

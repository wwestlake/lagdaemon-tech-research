#pragma once
#include "Mechanical.h"
#include <array>

namespace mech
{
// Sinusoidal PM reference model. R/L and inertia are measured inputs; K is
// user supplied or explicitly calibrated to a static holding-torque assumption.
struct StepperModel
{
    int phases=2;
    double resistance=0, inductance=0, torqueConstant=0, emfConstant=0;
    double rotorInertia=0, damping=0, electricalCycles=0, gearRatio=1;
};
std::string problemsWith(const StepperModel&,const MotorLoad&);
struct StepperMotor
{
    circuit_sim::Node shaft=0,angle=0,outputShaft=0,outputAngle=0;
    std::vector<int> windings;
    std::vector<Quantity> quantities;
};
// Pairs are phase +/-. Four-phase unipolar uses COM as each + terminal.
StepperMotor addStepperMotor(circuit_sim::Circuit&,const std::string&,const std::vector<std::pair<circuit_sim::Node,circuit_sim::Node>>&,
                            const StepperModel&,const MotorLoad&,const ShaftModel&);

// Averaged, bus-compliant current-limited bridge. No PWM/UART/thermal model.
// Pins: VM, GND, STEP+, STEP-, DIR+, DIR-, DISABLE+, DISABLE-, A+, A-, B+, B-.
struct StepDriverModel
{
    double minSupply=4.75,maxSupply=29,currentPeak=1.414,maxCurrentPeak=2.8;
    double logicHigh=2,logicLow=.8,minPulse=100e-9,directionSetup=20e-9;
    int microsteps=16;
};
class StepDriverDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit StepDriverDevice(StepDriverModel);
    void reset() override;
    bool evaluate(double,double,const std::vector<double>&,std::vector<double>&,std::string&) override;
    void accept(double) override;
    bool saveState(std::vector<std::uint8_t>&) const override;
    bool restoreState(const std::vector<std::uint8_t>&) override;
    long long stepCount() const { return static_cast<long long>(state[0]); }
private:
    StepDriverModel m;
    // count, STEP level, DIR level, last STEP transition time, last DIR transition time.
    std::array<double,5> state{},trial{};
};
// Four low-side sinks plus clamp diodes. Circuit switches are the existing
// internal model; ron/drop are explicit approximations, not a thermal rating.
std::vector<int> addUnipolarDriver(circuit_sim::Circuit&,const std::string&,circuit_sim::Node common,circuit_sim::Node ground,
                                  const std::array<circuit_sim::Node,4>& inputs,const std::array<circuit_sim::Node,4>& outputs,
                                  double threshold,double onResistance,double saturationVoltage);
}

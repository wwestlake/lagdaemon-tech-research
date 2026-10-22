#pragma once
#include "StepperCatalog.h"
#include "StepperMotor.h"
#include <functional>
#include <cmath>
#include <set>

namespace stepper_catalog
{
struct BuildResult
{
    int element=-1;
    circuit_sim::Node speed=0,angle=0;
    std::vector<mech::Quantity> quantities;
    juce::String error,warning;
};
// Shared by schematic analysis and persisted schematic subsystem compilation.
inline BuildResult build(circuit_sim::Circuit& c,const juce::String& symbol,const std::string& name,
                         const std::function<circuit_sim::Node(const char*)>& pin,const std::function<juce::String(const char*)>& raw)
{
    BuildResult out;
    if(!isMotor(symbol)&&!isDriver(symbol)){out.error="Unknown stepper component.";return out;}
    const auto number=[&](const char* key,double fallback=0.0)
    {const auto value=raw(key).trim();double result=fallback;if(value.isNotEmpty()&&(!circuit_sim::parseValue(value.toStdString(),result)||!std::isfinite(result)))out.error="Invalid stepper parameter: "+juce::String(key);return result;};
    try
    {
        if(const auto* reference=find(symbol))
        {
            mech::StepperModel model;model.phases=isUnipolar(symbol)?4:2;
            model.resistance=number("phase_r",reference->resistance);model.inductance=number("phase_l",reference->inductance);model.rotorInertia=number("rotor_j",reference->inertia);
            model.electricalCycles=number("electrical_cycles",reference->fullStep>0?90/reference->fullStep:0);model.gearRatio=number("gear_ratio",reference->ratio);model.damping=number("damping");
            const auto calibration=raw("calibration");
            if(calibration=="Holding-torque sine reference"||(calibration.isEmpty()&&reference->holding>0))
            {
                const double holding=number("holding_torque",reference->holding),rated=number("rated_current",reference->current);
                if(!(holding>0&&rated>0))out.error="Holding-reference calibration requires positive published holding torque and phase current.";
                else model.torqueConstant=model.emfConstant=holding/(std::sqrt(2.0)*rated);
                out.warning="Sinusoidal reference calibrated to two equal rated phase currents. Kt/Ke are model assumptions; no measured speed-torque, detent, saturation or thermal prediction.";
            }
            else{model.torqueConstant=number("kt");model.emfConstant=number("ke");out.warning="User-supplied electromechanical coefficients. Ideal sinusoidal phases and lossless output reduction only.";}
            mech::MotorLoad load;load.inertia=number("load_inertia");load.damping=number("load_damping");load.torque.offset=number("load_torque");
            mech::ShaftModel initial;initial.theta0=number("initial_position");initial.omega0=number("initial_speed");
            if(const auto problem=mech::problemsWith(model,load);!problem.empty())out.error=problem;
            if(!out.error.isEmpty())return out;
            std::vector<std::pair<circuit_sim::Node,circuit_sim::Node>> terminals;
            if(model.phases==4)for(const auto* phase:{"A","B","C","D"})terminals.push_back({pin("COM"),pin(phase)});
            else terminals={{pin("A+"),pin("A-")},{pin("B+"),pin("B-")}};
            const auto motor=mech::addStepperMotor(c,name,terminals,model,load,initial);out.element=motor.windings.front();out.speed=motor.outputShaft;out.angle=motor.outputAngle;out.quantities=motor.quantities;
            if(std::abs(model.torqueConstant-model.emfConstant)>1e-9)out.warning+=" User Kt and Ke differ; ideal conversion is not energy reciprocal.";
        }
        else if(symbol=="driver_uln2003")
        {
            const auto elements=mech::addUnipolarDriver(c,name,pin("COM"),pin("GND"),{pin("IN1"),pin("IN2"),pin("IN3"),pin("IN4")},{pin("OUT1"),pin("OUT2"),pin("OUT3"),pin("OUT4")},number("threshold",2.4),number("ron",1),number("vsat",1));
            out.element=elements.front();out.warning="ULN2003A reference: switch array with clamp diodes, not a chopper. Package/current/thermal limits and exact carrier pinout remain specification-only.";
        }
        else
        {
            const bool dm=symbol=="driver_dm542t_v4";mech::StepDriverModel model;
            model.minSupply=dm?18:4.75;model.maxSupply=dm?50:29;model.maxCurrentPeak=dm?4.5:2.8;model.currentPeak=number("current_peak",dm?2.84:1.414);
            model.logicHigh=number("logic_high",dm?4.5:2);model.logicLow=dm?.5:.8;model.minPulse=number("min_pulse",dm?2.5e-6:100e-9);model.directionSetup=number("dir_setup",dm?5e-6:20e-9);
            const double micro=number("microsteps",16);
            if(micro<1||micro>256){out.error="Driver microstep count must be within 1..256.";return out;}
            model.microsteps=(int)micro;
            const std::set<int> dmModes{1,2,4,5,8,10,16,20,25,32,40,50,64,100,125,128};
            if(micro!=model.microsteps||(dm&&!dmModes.count(model.microsteps))||(!dm&&(model.microsteps&(model.microsteps-1))!=0)||model.minPulse<(dm?2.5e-6:100e-9)||model.directionSetup<(dm?5e-6:20e-9)||(dm&&(model.logicHigh<4.5||model.logicHigh>5)))out.error="Driver microstep selection, 5V input threshold or pulse timing is outside the modeled manufacturer's limits.";
            if(!out.error.isEmpty())return out;
            std::vector<circuit_sim::Node> nodes{pin("VM"),pin("GND"),pin("STEP+"),pin("STEP-"),pin("DIR+"),pin("DIR-"),pin("DIS+"),pin("DIS-"),pin("A+"),pin("A-"),pin("B+"),pin("B-")};
            for(int k=8;k<12;++k)c.addResistor(name+".common_mode"+std::to_string(k),nodes[k],pin("GND"),1e9);
            out.element=c.addProgrammable(name,nodes,std::make_shared<mech::StepDriverDevice>(model));
            out.warning="Averaged, bus-compliant current limiting and STEP/DIR/disable. Peak current, not RMS. No actual chopping/UART/diagnostics, thermal limits or module pinout. DM reference assumes S2=5V; direct 3.3V cannot assert its high input.";
        }
    }
    catch(const std::exception& exception){out.error=exception.what();}
    return out;
}
}

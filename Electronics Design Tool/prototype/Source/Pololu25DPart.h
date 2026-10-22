#pragma once
#include "Pololu25DMotor.h"
#include <JuceHeader.h>
#include <functional>

namespace pololu25d
{
struct BuildResult
{
    int element=-1; circuit_sim::Node speed=0,angle=0;
    std::vector<mech::Quantity> quantities;juce::String error,warning;
};
inline BuildResult build(circuit_sim::Circuit& c,const Reference& reference,const std::string& name,
    const std::function<circuit_sim::Node(const char*)>& pin,const std::function<juce::String(const char*)>& raw)
{
    BuildResult out;const bool fit=raw("calibration")=="Datasheet fitted lines";
    const auto number=[&](const char* key,double fallback)
    {const auto text=raw(key).trim();double value=fallback;if(text.isNotEmpty()&&(!circuit_sim::parseValue(text.toStdString(),value)||!std::isfinite(value)))out.error="Invalid Pololu parameter: "+juce::String(key);return value;};
    // Source inputs are frozen by ordinary diagram parameter persistence, so a
    // later catalog release cannot silently recalibrate an existing occurrence.
    auto snapshot=reference;
    snapshot.rpm=number("reference_rpm",reference.rpm);snapshot.noLoadCurrent=number("reference_i0",reference.noLoadCurrent);
    snapshot.stallCurrent=number("reference_is",reference.stallCurrent);snapshot.stallKgCm=number("reference_ts_kgcm",reference.stallKgCm);
    snapshot.fitSpeedSlope=number("reference_fit_speed",reference.fitSpeedSlope);snapshot.fitCurrentIntercept=number("reference_fit_i0",reference.fitCurrentIntercept);snapshot.fitCurrentSlope=number("reference_fit_i_slope",reference.fitCurrentSlope);
    if(!(snapshot.rpm>0&&snapshot.noLoadCurrent>=0&&snapshot.stallCurrent>snapshot.noLoadCurrent&&snapshot.stallKgCm>0&&snapshot.fitSpeedSlope>0&&snapshot.fitCurrentIntercept>=0&&snapshot.fitCurrentSlope>0))out.error="Invalid persisted manufacturer reference inputs.";
    if(out.error.isNotEmpty())return out;
    auto m=fit?curveFitModel(snapshot):endpointModel(snapshot);
    const bool user=raw("calibration")=="User coefficients";
    if(user){m.ra=number("ra",m.ra);m.kt=number("kt",m.kt);m.ke=number("ke",m.ke);m.b=number("b",m.b);}
    m.la=number("la",0);m.j=number("j",1e-5);
    mech::MotorLoad load;load.inertia=number("load_inertia",0);load.damping=number("load_damping",0);load.torque.offset=number("load_torque",0);
    mech::ShaftModel initial;initial.theta0=number("initial_position",0);initial.omega0=number("initial_speed",0);
    if(const auto problem=mech::problemsWith(m,load);!problem.empty())out.error=problem;
    if(const auto problem=mech::problemsWith(initial);!problem.empty())out.error=problem;
    double ratio=encoderRatio,rout=100;
    if(reference.encoder){ratio=number("gear_ratio",encoderRatio);rout=number("encoder_rout",100);if(!(ratio>0&&rout>0))out.error="Encoder ratio and reference output resistance must be positive.";}
    if(out.error.isNotEmpty())return out;
    const auto motor=mech::addDcMotor(c,name,pin("M1"),pin("M2"),m,load,initial);
    out.element=motor.armature;out.speed=motor.shaft;out.angle=motor.angle;out.quantities=motor.quantities;
    if(reference.encoder)
    {
        const auto ground=pin("GND");
        c.addProgrammable(name+".encoder",{motor.angle,pin("VCC"),ground,pin("A"),pin("B")},std::make_shared<QuadratureEncoder>(ratio,rout));
        c.addResistor(name+".encoder_A_leak",pin("A"),ground,1e9);c.addResistor(name+".encoder_B_leak",pin("B"),ground,1e9);
        for(const auto& term:{std::pair<const char*,circuit_sim::Node>{"rotor_position",motor.angle},{"rotor_speed",motor.shaft}})
        {mech::Quantity q;q.name=name+"."+term.first;q.unit=term.second==motor.angle?"rad":"rad/s";q.description="Calculated motor-shaft telemetry from output angle/speed and encoder-count-consistent ratio";q.gain=ratio;mech::Factor f;f.node=term.second;q.factors={f};out.quantities.push_back(q);}
    }
    out.warning=user?"User coefficients in an output-shaft-equivalent DC model. ":fit?"Manufacturer plotted fitted-line calibration (not product typical endpoints); calculated R/Kt/Ke/B are not intrinsic motor constants. ":"Manufacturer typical endpoint calibration in an output-shaft-equivalent DC model; calculated R/Kt/Ke/B are not measured intrinsic motor constants. ";
    out.warning+="L=0 and J=1e-5 defaults are reference assumptions; transient timing, gearbox loss/backlash and thermal/damage limits are not characterized. Published stall values are extrapolations, not safe working ratings.";
    return out;
}
}

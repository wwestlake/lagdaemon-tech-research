#include "StepperMotor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace mech
{
namespace
{
constexpr double pi=3.14159265358979323846;
Factor voltage(circuit_sim::Node node){Factor f;f.node=node;return f;}
Factor current(int element){Factor f;f.kind=Factor::Kind::Current;f.element=element;return f;}
class StepperCoupling final : public circuit_sim::ProgrammableDevice
{
    StepperModel m;
public:
    explicit StepperCoupling(StepperModel model):m(model){}
    void reset() override {}
    void accept(double) override {}
    bool saveState(std::vector<std::uint8_t>& out)const override{out.clear();return true;}
    bool restoreState(const std::vector<std::uint8_t>& in)override{return in.empty();}
    bool evaluate(double,double,const std::vector<double>& v,std::vector<double>& i,std::string& error) override
    {
        if(v.size()!=3+3*m.phases){error="Invalid stepper coupling pin count.";return false;}
        i.assign(v.size(),0);
        for(int k=0;k<m.phases;++k)
        {
            const int base=3+3*k;
            const double factor=-std::sin(m.electricalCycles*v[1]-k*pi/2);
            const double emf=m.emfConstant*factor*v[0];
            const double drive=(emf-(v[base]-v[base+1]))/1e-4;
            i[base]=drive;i[base+1]=-drive;
            i[0]+=m.torqueConstant*factor*v[base+2];
        }
        // A quantity node exposes calculated torque without asking generic
        // semiconductor terminal-current reporting to evaluate this device.
        i[2]=(i[0]-v[2])/1e-4;
        return true;
    }
};
}
std::string problemsWith(const StepperModel& m,const MotorLoad& load)
{
    if(m.phases!=2&&m.phases!=4)return "A stepper needs two bipolar or four unipolar phases.";
    for(double value:{m.resistance,m.inductance,m.torqueConstant,m.emfConstant,m.rotorInertia,m.electricalCycles,m.gearRatio})
        if(!std::isfinite(value)||value<=0)return "Stepper simulation requires positive finite winding R/L, Kt/Ke, rotor inertia, electrical cycles and gear ratio. Unknown specifications must be supplied explicitly.";
    for(double value:{m.damping,load.inertia,load.damping})if(!std::isfinite(value)||value<0)return "Stepper damping/load inertia must be finite and nonnegative.";
    return {};
}
StepperMotor addStepperMotor(circuit_sim::Circuit& c,const std::string& name,const std::vector<std::pair<circuit_sim::Node,circuit_sim::Node>>& terminals,
                            const StepperModel& m,const MotorLoad& load,const ShaftModel& initial)
{
    const auto problem=problemsWith(m,load);if(!problem.empty()||terminals.size()!=m.phases)throw std::invalid_argument(problem.empty()?"Stepper terminal count mismatch.":problem);
    if(const auto shaftProblem=problemsWith(initial);!shaftProblem.empty())throw std::invalid_argument(shaftProblem);
    StepperMotor out;out.shaft=c.addQuantityNode();out.angle=c.addQuantityNode();
    const auto torque=c.addQuantityNode();
    std::vector<circuit_sim::Node> pins{out.shaft,out.angle,torque};
    const auto add=[&](const std::string& key,const std::string& unit,std::vector<Factor> f,double gain=1.0){Quantity q;q.name=name+"."+key;q.unit=unit;q.description="Sinusoidal stepper reference model: "+key;q.factors=std::move(f);q.gain=gain;out.quantities.push_back(q);};
    for(int k=0;k<m.phases;++k)
    {
        const auto rnode=c.addNode(),emf=c.addNode(),sense=c.addQuantityNode();const auto suffix=".phase"+std::to_string(k+1);
        c.addResistor(name+suffix+".R",terminals[k].first,rnode,m.resistance);
        const int winding=c.addInductor(name+suffix,rnode,emf,m.inductance);out.windings.push_back(winding);
        auto& inductance=c.elements()[winding];inductance.hasInitialCondition=true;inductance.initialCondition=0;inductance.holdInitialCondition=true;
        c.addCcvs(name+suffix+".sense",sense,0,winding,1);
        pins.insert(pins.end(),{emf,terminals[k].second,sense});add("current"+std::to_string(k+1),"A",{current(winding)});
    }
    c.addProgrammable(name+".coupling",pins,std::make_shared<StepperCoupling>(m));
    add("rotor_torque","N*m",{voltage(torque)});add("torque","N*m",{voltage(torque)},m.gearRatio);
    const double g=m.gearRatio;
    const int inertia=c.addCapacitor(name+".inertia",out.shaft,0,m.rotorInertia+load.inertia/(g*g));
    auto& cap=c.elements()[inertia];cap.hasInitialCondition=true;cap.initialCondition=initial.omega0;cap.holdInitialCondition=true;
    if(m.damping+load.damping/(g*g)>0)c.addResistor(name+".damping",out.shaft,0,1/(m.damping+load.damping/(g*g)));
    auto reflected=load.torque;reflected.offset/=g;reflected.amplitude/=g;for(auto& point:reflected.points)point.second/=g;
    c.addCurrentSource(name+".load",out.shaft,0,reflected);
    c.addProgrammable(name+".shaft",{out.shaft,out.angle},std::make_shared<ShaftDevice>(initial));
    out.outputShaft=c.addQuantityNode();out.outputAngle=c.addQuantityNode();
    c.addVcvs(name+".ideal_gear_speed",out.outputShaft,0,out.shaft,0,1/g);c.addVcvs(name+".ideal_gear_angle",out.outputAngle,0,out.angle,0,1/g);
    add("speed","rad/s",{voltage(out.outputShaft)});add("position","rad",{voltage(out.outputAngle)});add("rotor_speed","rad/s",{voltage(out.shaft)});add("rotor_position","rad",{voltage(out.angle)});
    return out;
}
StepDriverDevice::StepDriverDevice(StepDriverModel model):m(model)
{
    for(double value:{m.minSupply,m.maxSupply,m.currentPeak,m.maxCurrentPeak,m.logicHigh,m.logicLow,m.minPulse,m.directionSetup})if(!std::isfinite(value))throw std::invalid_argument("Driver settings must be finite.");
    if(!(m.currentPeak>0&&m.currentPeak<=m.maxCurrentPeak&&m.microsteps>0&&m.microsteps<=256&&m.logicHigh>m.logicLow&&m.minSupply>0&&m.maxSupply>m.minSupply))throw std::invalid_argument("Invalid stepper driver limits/current/microstep settings.");
    if(!(m.minPulse>0&&m.directionSetup>=0))throw std::invalid_argument("Driver timing settings are invalid.");
    reset();
}
void StepDriverDevice::reset(){state={0,0,0,-1e9,-1e9};trial=state;}
bool StepDriverDevice::evaluate(double t,double h,const std::vector<double>& v,std::vector<double>& i,std::string& error)
{
    if(v.size()!=12){error="Stepper driver needs 12 terminals.";return false;}
    const double bus=v[0]-v[1];
    if(bus>m.maxSupply+.01){error="Stepper driver supply exceeds the specified operating maximum.";return false;}
    const bool step=v[2]-v[3]>=m.logicHigh,dir=v[4]-v[5]>=m.logicHigh,disabled=v[6]-v[7]>=m.logicHigh;
    trial=state;
    if(h>0)
    {
        if(dir!=(state[2]!=0)){trial[2]=dir;trial[4]=t;}
        if(step!=(state[1]!=0))
        {
            if(t-state[3]<m.minPulse){error="STEP high/low interval is shorter than the driver's minimum pulse width.";return false;}
            trial[1]=step;trial[3]=t;
            if(step&&!disabled&&bus>=m.minSupply)
            {if(t-trial[4]<m.directionSetup){error="DIR setup time before STEP is too short.";return false;}trial[0]+=dir?-1:1;}
        }
    }
    i.assign(12,0);
    const bool enabled=!disabled&&bus>=m.minSupply;
    if(bus<0){error="Stepper driver supply polarity is reversed.";return false;}
    const double phase=pi/4+trial[0]*pi/(2*m.microsteps);
    for(int k=0;k<2;++k)
    {
        const int p=8+2*k,n=p+1;const double target=m.currentPeak*(k==0?std::cos(phase):std::sin(phase));
        const double differential=v[p]-v[n];
        double drive=enabled?std::clamp(((target>=0?bus:-bus)-differential)/.1,-std::abs(target),std::abs(target)):0;
        // Regularized bridge freewheel path allows stored winding energy to
        // return to the bus during reversal or a current-target reduction.
        if(differential>bus)drive+=(bus-differential)/.1;
        if(differential< -bus)drive+=(-bus-differential)/.1;
        i[p]=drive;i[n]=-drive;
        const double power=drive*(v[p]-v[n]);
        if(bus>1e-12){i[0]-=power/bus;i[1]+=power/bus;}
    }
    return true;
}
void StepDriverDevice::accept(double){state=trial;}
bool StepDriverDevice::saveState(std::vector<std::uint8_t>& out)const{out.resize(sizeof(state));std::memcpy(out.data(),state.data(),sizeof(state));return true;}
bool StepDriverDevice::restoreState(const std::vector<std::uint8_t>& in){if(in.size()!=sizeof(state))return false;std::memcpy(state.data(),in.data(),sizeof(state));trial=state;return true;}
std::vector<int> addUnipolarDriver(circuit_sim::Circuit& c,const std::string& name,circuit_sim::Node common,circuit_sim::Node ground,
                                  const std::array<circuit_sim::Node,4>& inputs,const std::array<circuit_sim::Node,4>& outputs,double threshold,double ron,double drop)
{
    if(!(std::isfinite(threshold)&&std::isfinite(ron)&&std::isfinite(drop)&&threshold>0&&ron>0&&drop>=0))throw std::invalid_argument("ULN reference switch threshold/resistance/drop are invalid.");
    std::vector<int> result;
    for(int k=0;k<4;++k)
    {
        const auto sink=c.addNode();circuit_sim::Waveform saturation;saturation.offset=drop;c.addVoltageSource(name+".drop"+std::to_string(k),sink,ground,saturation);
        result.push_back(c.addVoltageControlledSwitch(name+".switch"+std::to_string(k),outputs[k],sink,inputs[k],ground,ron,1e9,threshold,0));
        c.addDiode(name+".clamp"+std::to_string(k),outputs[k],common);
    }
    return result;
}
}

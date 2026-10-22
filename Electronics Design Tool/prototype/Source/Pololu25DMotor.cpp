#include "Pololu25DMotor.h"
#include <cmath>
#include <stdexcept>

namespace pololu25d
{
constexpr double pi=3.14159265358979323846;
mech::DcMotorModel endpointModel(const Reference& r)
{
    mech::DcMotorModel m;const double omega=r.rpm*2*pi/60;
    m.ra=12/r.stallCurrent;m.kt=r.stallKgCm*kgCmToNm/r.stallCurrent;
    m.ke=(12-m.ra*r.noLoadCurrent)/omega;m.b=m.kt*r.noLoadCurrent/omega;
    // Explicit reference assumptions: no winding inductance; arbitrary positive
    // output-equivalent inertia required by the existing dynamic motor interface.
    // Neither is manufacturer data; transient timing is not calibrated.
    m.la=0;m.j=1e-5;return m;
}
mech::DcMotorModel curveFitModel(const Reference& r)
{
    auto equivalent=r;
    const double zeroSpeedTorqueKgMm=r.rpm/r.fitSpeedSlope;
    equivalent.noLoadCurrent=r.fitCurrentIntercept;
    equivalent.stallCurrent=r.fitCurrentIntercept+r.fitCurrentSlope*zeroSpeedTorqueKgMm;
    equivalent.stallKgCm=zeroSpeedTorqueKgMm/10;
    return endpointModel(equivalent);
}
QuadratureEncoder::QuadratureEncoder(double g,double r):ratio(g),resistance(r)
{
    if(!std::isfinite(g)||g<=0||!std::isfinite(r)||r<=0)throw std::invalid_argument("Encoder ratio and reference output resistance must be positive finite values.");
}
bool QuadratureEncoder::evaluate(double,double,const std::vector<double>& v,std::vector<double>& i,std::string& error)
{
    if(v.size()!=5){error="Quadrature encoder needs angle/VCC/GND/A/B.";return false;}
    const double supply=v[1]-v[2];i.assign(5,0);
    if(supply>20){error="Pololu encoder supply exceeds 20 V.";return false;}
    if(supply<0){error="Pololu encoder supply polarity is reversed.";return false;}
    if(supply<3.5)return true; // unpowered/undervoltage outputs are high impedance
    const double phase=std::remainder(v[0]*ratio*12,2*pi);
    for(int k=0;k<2;++k)
    {
        const bool high=std::sin(phase-k*pi/2)>=0;const int rail=high?1:2;
        i[3+k]=(v[rail]-v[3+k])/resistance;i[rail]-=i[3+k];
    }
    return true;
}
}

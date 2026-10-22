#pragma once
#include "Pololu25DCatalog.h"
#include "Mechanical.h"

namespace pololu25d
{
// An output-shaft equivalent, not intrinsic winding/rotor constants.
// R=12/Is; Kt=Ts/Is; Ke=(12-R*I0)/w0; B=Kt*I0/w0.
mech::DcMotorModel endpointModel(const Reference&);
mech::DcMotorModel curveFitModel(const Reference&);

// Pins: output-shaft angle [rad], encoder VCC, GND, A, B.
// Ideal sampled quadrature; 12 cycles/rotor rev = 48 both-edge counts.
class QuadratureEncoder final:public circuit_sim::ProgrammableDevice
{
public:
    QuadratureEncoder(double motorToOutputRatio,double outputResistance);
    void reset() override {}
    void accept(double) override {}
    bool evaluate(double,double,const std::vector<double>&,std::vector<double>&,std::string&) override;
    bool saveState(std::vector<std::uint8_t>& out) const override {out.clear();return true;}
    bool restoreState(const std::vector<std::uint8_t>& in) override {return in.empty();}
private:
    double ratio,resistance;
};
}

#include "Mechanical.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mech
{
namespace
{
using circuit_sim::Circuit;
using circuit_sim::Element;
using circuit_sim::Node;

constexpr double pi = 3.14159265358979323846;

Factor voltage(Node a, Node b = 0) { Factor f; f.kind = Factor::Kind::Voltage; f.node = a; f.node2 = b; return f; }
Factor current(int element) { Factor f; f.kind = Factor::Kind::Current; f.element = element; return f; }
Factor value(int element) { Factor f; f.kind = Factor::Kind::Value; f.element = element; return f; }
} // namespace

// ---- shaft --------------------------------------------------------------------------

std::string problemsWith(const ShaftModel& m)
{
    std::string p;
    auto add = [&](const std::string& s) { p += (p.empty() ? "" : " ") + s; };
    if (!(m.coulomb >= 0.0)) add("Coulomb friction must be 0 or more (N*m).");
    if (!(m.smoothing > 0.0)) add("The friction smoothing speed must be greater than 0 (rad/s).");
    if ((m.hasMin || m.hasMax) && !(m.stopStiffness > 0.0)) add("End-stop stiffness must be greater than 0 (N*m/rad).");
    if ((m.hasMin || m.hasMax) && !(m.stopDamping >= 0.0)) add("End-stop damping must be 0 or more (N*m*s/rad).");
    if (m.hasMin && m.hasMax && !(m.minAngle < m.maxAngle)) add("The minimum angle must be below the maximum angle.");
    if (m.hasMin && m.theta0 < m.minAngle) add("The initial angle is below the minimum angle (end stop).");
    if (m.hasMax && m.theta0 > m.maxAngle) add("The initial angle is above the maximum angle (end stop).");
    if (!std::isfinite(m.theta0) || !std::isfinite(m.omega0)) add("The initial angle and speed must be finite numbers.");
    return p;
}

ShaftDevice::ShaftDevice(ShaftModel model) : m(model) { reset(); }

void ShaftDevice::reset()
{
    theta = trialTheta = m.theta0;
    omega = trialOmega = m.omega0;
}

double ShaftDevice::torque(double th, double w) const
{
    double t = 0.0;
    if (m.coulomb > 0.0)
        t -= m.coulomb * std::tanh(w / m.smoothing);
    // End stops: a stiff spring-damper while past the stop, pushing back only.
    if (m.hasMax && th > m.maxAngle)
        t -= std::max(0.0, m.stopStiffness * (th - m.maxAngle) + m.stopDamping * w);
    if (m.hasMin && th < m.minAngle)
        t += std::max(0.0, m.stopStiffness * (m.minAngle - th) - m.stopDamping * w);
    return t;
}

bool ShaftDevice::evaluate(double t, double h, const std::vector<double>& v, std::vector<double>& i, std::string& error)
{
    (void)t;
    if (v.size() != 2)
    {
        error = "a shaft has two pins (speed, angle)";
        return false;
    }
    const auto w = v[0];
    // Trapezoidal angle from the committed state; in DC the angle is held
    // (the shaft turns at its steady speed) and the speed history a transient
    // starts from is the initial speed (the inertia's initial condition).
    const auto th = h > 0.0 ? theta + 0.5 * h * (omega + w) : theta;
    trialTheta = th;
    trialOmega = h > 0.0 ? w : m.omega0;
    i.assign(2, 0.0);
    i[0] = torque(th, w);
    i[1] = th - v[1]; // drives the angle node to th through 1 (nothing else loads it)
    return true;
}

void ShaftDevice::accept(double t)
{
    (void)t;
    theta = trialTheta;
    omega = trialOmega;
}

bool ShaftDevice::saveState(std::vector<std::uint8_t>& out) const
{
    const double v[2] { theta, omega };
    out.resize(sizeof v);
    std::memcpy(out.data(), v, sizeof v);
    return true;
}

bool ShaftDevice::restoreState(const std::vector<std::uint8_t>& in)
{
    double v[2];
    if (in.size() != sizeof v)
        return false;
    std::memcpy(v, in.data(), sizeof v);
    theta = trialTheta = v[0];
    omega = trialOmega = v[1];
    return true;
}

// ---- quantities ------------------------------------------------------------------------

bool Quantity::linear() const
{
    int solved = 0;
    for (const auto& f : factors)
        solved += f.kind == Factor::Kind::Value ? 0 : 1;
    return solved == 1;
}

double valueOf(const Quantity& q, const Circuit& c, const circuit_sim::OperatingPoint& op, double t)
{
    double v = q.gain;
    for (const auto& f : q.factors)
    {
        switch (f.kind)
        {
            case Factor::Kind::Voltage:
                v *= op.voltages[(size_t)f.node] - op.voltages[(size_t)f.node2];
                break;
            case Factor::Kind::Value:
                v *= c.elements()[(size_t)f.element].value;
                break;
            case Factor::Kind::Current:
            {
                const auto& e = c.elements()[(size_t)f.element];
                if (e.type == Element::Type::CurrentSource)
                    v *= e.wave.valueAt(t);
                else
                {
                    const auto currents = circuit_sim::terminalCurrents(c, op, f.element);
                    v *= currents.empty() ? 0.0 : currents[0];
                }
                break;
            }
        }
    }
    return v;
}

// ---- DC motor ----------------------------------------------------------------------------

std::string problemsWith(const DcMotorModel& m, const MotorLoad& load)
{
    std::string p;
    auto add = [&](const std::string& s) { p += (p.empty() ? "" : " ") + s; };
    if (!(m.ra > 0.0)) add("Armature resistance must be greater than 0 (ohm).");
    if (!(m.la >= 0.0)) add("Armature inductance must be 0 or more (H).");
    if (!(m.kt > 0.0)) add("The torque constant must be greater than 0 (N*m/A).");
    if (!(m.ke > 0.0)) add("The back-EMF constant must be greater than 0 (V*s/rad).");
    if (!(m.j > 0.0)) add("Rotor inertia must be greater than 0 (kg*m^2).");
    if (!(m.b >= 0.0)) add("Rotor damping must be 0 or more (N*m*s/rad).");
    if (!(load.inertia >= 0.0)) add("Load inertia must be 0 or more (kg*m^2).");
    if (!(load.damping >= 0.0)) add("Load damping must be 0 or more (N*m*s/rad).");
    if (!std::isfinite(m.i0)) add("The initial current must be a finite number.");
    return p;
}

DcMotor addDcMotor(Circuit& c, const std::string& name, Node plus, Node minus, const DcMotorModel& model, const MotorLoad& load,
                   const ShaftModel& shaftModel)
{
    DcMotor d;
    d.shaft = c.addQuantityNode();
    d.angle = c.addQuantityNode();
    const auto a1 = c.addNode(), a2 = c.addNode();

    // Armature: + -> Ra -> La -> back EMF (Ke * omega) -> -.
    d.resistance = c.addResistor(name + ".ra", plus, a1, model.ra);
    if (model.la > 0.0)
    {
        d.armature = c.addInductor(name, a1, a2, model.la);
        auto& l = c.elements()[(size_t)d.armature];
        l.hasInitialCondition = true;
        l.initialCondition = model.i0;
        l.holdInitialCondition = true;
    }
    else
    {
        circuit_sim::Waveform zero;
        d.armature = c.addVoltageSource(name, a1, a2, zero); // a current sense
    }
    d.emf = c.addVcvs(name + ".emf", a2, minus, d.shaft, 0, model.ke);

    // Shaft: Kt * I in, J to ground, B to ground, the load torque out.
    d.torque = c.addCccs(name + ".kt", 0, d.shaft, d.armature, model.kt);
    d.inertia = c.addCapacitor(name + ".j", d.shaft, 0, model.j + load.inertia);
    {
        auto& j = c.elements()[(size_t)d.inertia];
        j.hasInitialCondition = true;
        j.initialCondition = shaftModel.omega0;
        j.holdInitialCondition = true;
    }
    if (const auto b = model.b + load.damping; b > 0.0)
        d.damping = c.addResistor(name + ".b", d.shaft, 0, 1.0 / b);
    d.load = c.addCurrentSource(name + ".load", d.shaft, 0, load.torque);
    d.device = c.addProgrammable(name + ".shaft", { d.shaft, d.angle }, std::make_shared<ShaftDevice>(shaftModel));
    c.elements()[(size_t)d.device].modelName = "MECH_SHAFT";

    auto add = [&](const std::string& key, const std::string& unit, const std::string& what, double gain, std::vector<Factor> f) {
        Quantity q;
        q.name = name + "." + key;
        q.unit = unit;
        q.description = what;
        q.gain = gain;
        q.factors = std::move(f);
        d.quantities.push_back(std::move(q));
    };
    add("speed", "rad/s", "shaft angular velocity", 1.0, { voltage(d.shaft) });
    add("rpm", "rpm", "shaft speed in revolutions per minute", 60.0 / (2.0 * pi), { voltage(d.shaft) });
    add("position", "rad", "shaft angle (integral of the speed from the initial angle)", 1.0, { voltage(d.angle) });
    add("current", "A", "armature current, into + through the motor", 1.0, { current(d.armature) });
    add("voltage", "V", "terminal voltage, + to -", 1.0, { voltage(plus, minus) });
    add("backemf", "V", "back EMF, Ke * speed", 1.0, { value(d.emf), voltage(d.shaft) });
    add("torque", "N*m", "electromagnetic torque, Kt * current", 1.0, { value(d.torque), current(d.armature) });
    add("power_in", "W", "electrical power into the terminals", 1.0, { voltage(plus, minus), current(d.armature) });
    add("power_copper", "W", "armature resistance loss, Ra * current^2", 1.0, { value(d.resistance), current(d.armature), current(d.armature) });
    add("power_converted", "W", "electromechanical power, torque * speed (negative: generating)", 1.0,
        { value(d.torque), current(d.armature), voltage(d.shaft) });
    if (d.damping >= 0)
        add("power_damping", "W", "viscous loss, (rotor + load damping) * speed^2", 1.0, { voltage(d.shaft), current(d.damping) });
    add("power_load", "W", "power into the load torque, load torque * speed", 1.0, { voltage(d.shaft), current(d.load) });
    add("energy_kinetic", "J", "kinetic energy, (rotor + load inertia) * speed^2 / 2", 0.5, { value(d.inertia), voltage(d.shaft), voltage(d.shaft) });
    return d;
}

int addTachometer(Circuit& c, const std::string& name, Node shaft, Node outPlus, Node outMinus, double k, double rOut)
{
    if (!(rOut > 0.0))
        return c.addVcvs(name, outPlus, outMinus, shaft, 0, k);
    const auto internal = c.addNode();
    const auto e = c.addVcvs(name, internal, outMinus, shaft, 0, k);
    c.addResistor(name + ".r", internal, outPlus, rOut);
    return e;
}
} // namespace mech

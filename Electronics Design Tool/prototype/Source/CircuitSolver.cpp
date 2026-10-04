#include "CircuitSolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace circuit_sim
{
namespace
{
constexpr double thermalVoltage = 0.025852; // kT/q at 300 K
constexpr double pi = 3.14159265358979323846;

// ---- dense linear algebra ---------------------------------------------------

template <typename T>
bool solveDense(std::vector<std::vector<T>> a, std::vector<T> b, std::vector<T>& x)
{
    const auto n = b.size();
    for (size_t col = 0; col < n; ++col)
    {
        size_t pivot = col;
        double best = std::abs(a[col][col]);
        for (size_t row = col + 1; row < n; ++row)
            if (std::abs(a[row][col]) > best)
            {
                best = std::abs(a[row][col]);
                pivot = row;
            }
        if (best < 1e-30)
            return false;
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        for (size_t row = col + 1; row < n; ++row)
        {
            const T factor = a[row][col] / a[col][col];
            if (factor == T(0))
                continue;
            for (size_t k = col; k < n; ++k)
                a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }
    x.assign(n, T(0));
    for (size_t i = n; i-- > 0;)
    {
        T sum = b[i];
        for (size_t k = i + 1; k < n; ++k)
            sum -= a[i][k] * x[k];
        x[i] = sum / a[i][i];
    }
    return true;
}

double safeExp(double x)
{
    // Linear continuation above 80 keeps Newton finite.
    if (x > 80.0)
        return std::exp(80.0) * (1.0 + (x - 80.0));
    return std::exp(x);
}

// ---- device models: terminal currents into the device -----------------------

double diodeCurrent(const DiodeModel& m, double vd)
{
    const auto nvt = m.emission * thermalVoltage;
    auto id = m.saturationCurrent * (safeExp(vd / nvt) - 1.0);
    if (m.breakdownVoltage > 0.0 && vd < -m.breakdownVoltage)
        id -= m.saturationCurrent * (safeExp(-(vd + m.breakdownVoltage) / nvt) - 1.0);
    return id;
}

void npnCurrents(const BjtModel& m, double vbe, double vbc, double& ic, double& ib)
{
    const auto ef = safeExp(vbe / thermalVoltage) - 1.0;
    const auto er = safeExp(vbc / thermalVoltage) - 1.0;
    ic = m.saturationCurrent * (ef - er) - m.saturationCurrent / m.betaReverse * er;
    ib = m.saturationCurrent / m.betaForward * ef + m.saturationCurrent / m.betaReverse * er;
}

double nmosDrainCurrent(const MosModel& m, double vgs, double vds)
{
    // vds >= 0 here.
    const auto vov = vgs - m.threshold;
    if (vov <= 0.0)
        return 0.0;
    if (vds < vov)
        return m.transconductance * (vov * vds - 0.5 * vds * vds) * (1.0 + m.lambda * vds);
    return 0.5 * m.transconductance * vov * vov * (1.0 + m.lambda * vds);
}

// Terminal currents (into the device) for a nonlinear element.
std::vector<double> deviceCurrents(const Element& e, const std::vector<double>& v)
{
    switch (e.type)
    {
        case Element::Type::Diode:
        {
            const auto id = diodeCurrent(e.diode, v[0] - v[1]);
            return { id, -id };
        }
        case Element::Type::Npn:
        case Element::Type::Pnp:
        {
            // nodes: collector, base, emitter
            const auto sign = e.type == Element::Type::Npn ? 1.0 : -1.0;
            double ic = 0.0, ib = 0.0;
            npnCurrents(e.bjt, sign * (v[1] - v[2]), sign * (v[1] - v[0]), ic, ib);
            ic *= sign;
            ib *= sign;
            return { ic, ib, -(ic + ib) };
        }
        case Element::Type::Nmos:
        case Element::Type::Pmos:
        {
            // nodes: drain, gate, source
            const auto sign = e.type == Element::Type::Nmos ? 1.0 : -1.0;
            auto vd = sign * v[0], vg = sign * v[1], vs = sign * v[2];
            double id = 0.0;
            if (vd >= vs)
                id = nmosDrainCurrent(e.mos, vg - vs, vd - vs);
            else
                id = -nmosDrainCurrent(e.mos, vg - vd, vs - vd);
            id *= sign;
            return { id, 0.0, -id };
        }
        default:
            return {};
    }
}

bool isNonlinear(Element::Type t)
{
    return t == Element::Type::Diode || t == Element::Type::Npn || t == Element::Type::Pnp
        || t == Element::Type::Nmos || t == Element::Type::Pmos || t == Element::Type::OpAmp;
}

bool needsBranch(Element::Type t)
{
    return t == Element::Type::VoltageSource || t == Element::Type::Inductor || t == Element::Type::Vcvs
        || t == Element::Type::Ccvs || t == Element::Type::OpAmp;
}

// Op amp output: rail-limited smooth saturation.
struct OpAmpTransfer
{
    double value;
    double slope;
};

OpAmpTransfer opampTransfer(const Element& e, double vin, double railPlus, double railMinus)
{
    auto high = railPlus - e.opamp.railDrop;
    auto low = railMinus + e.opamp.railDrop;
    if (high - low < 0.2)
    {
        high = 15.0 - e.opamp.railDrop;
        low = -15.0 + e.opamp.railDrop;
    }
    const auto mid = 0.5 * (high + low);
    const auto half = 0.5 * (high - low);
    const auto t = std::tanh(e.opamp.gain * vin / half);
    return { mid + half * t, e.opamp.gain * (1.0 - t * t) };
}

// ---- MNA system --------------------------------------------------------------

struct Layout
{
    int nodeUnknowns = 0;                 // nodes 1..N-1
    std::vector<int> branch;              // per element, -1 if none
    std::vector<std::vector<std::pair<int, double>>> mutual; // inductor element -> (other inductor element, M)
    int size = 0;
    bool nonlinear = false;
};

Layout makeLayout(const Circuit& c)
{
    Layout l;
    l.nodeUnknowns = c.nodeCount() - 1;
    l.size = l.nodeUnknowns;
    const auto& parts = c.elements();
    l.branch.assign(parts.size(), -1);
    l.mutual.assign(parts.size(), {});
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (needsBranch(parts[i].type))
            l.branch[i] = l.size++;
        l.nonlinear |= isNonlinear(parts[i].type);
    }
    for (const auto& e : parts)
        if (e.type == Element::Type::Coupling && e.control >= 0 && e.control2 >= 0)
        {
            const auto m = e.value * std::sqrt(parts[(size_t)e.control].value * parts[(size_t)e.control2].value);
            l.mutual[(size_t)e.control].push_back({ e.control2, m });
            l.mutual[(size_t)e.control2].push_back({ e.control, m });
        }
    return l;
}

int idx(Node n) { return n - 1; }

template <typename T>
struct System
{
    std::vector<std::vector<T>> a;
    std::vector<T> b;
    explicit System(int n) : a((size_t)n, std::vector<T>((size_t)n, T(0))), b((size_t)n, T(0)) {}
    void add(int r, int c, T v) { if (r >= 0 && c >= 0) a[(size_t)r][(size_t)c] += v; }
    void rhs(int r, T v) { if (r >= 0) b[(size_t)r] += v; }
    void conductance(Node p, Node q, T g)
    {
        add(idx(p), idx(p), g); add(idx(q), idx(q), g);
        add(idx(p), idx(q), -g); add(idx(q), idx(p), -g);
    }
    // Branch current k flows from p through the element to q.
    void branchIncidence(Node p, Node q, int k)
    {
        add(idx(p), k, T(1)); add(idx(q), k, T(-1));
    }
};

double nodeVoltage(const std::vector<double>& x, Node n) { return n == 0 ? 0.0 : x[(size_t)idx(n)]; }

enum class Mode { Dc, Transient };

struct ReactiveState
{
    std::vector<double> v; // capacitor voltage / inductor voltage
    std::vector<double> i; // capacitor current / inductor current
};

void stampNonlinear(System<double>& s, const Element& e, const std::vector<double>& x)
{
    std::vector<double> v;
    for (auto n : e.nodes) v.push_back(nodeVoltage(x, n));
    const auto i0 = deviceCurrents(e, v);
    const double delta = 1e-6;
    std::vector<std::vector<double>> j(e.nodes.size(), std::vector<double>(e.nodes.size(), 0.0));
    for (size_t u = 0; u < e.nodes.size(); ++u)
    {
        auto vp = v;
        vp[u] += delta;
        const auto ip = deviceCurrents(e, vp);
        for (size_t t = 0; t < e.nodes.size(); ++t)
            j[t][u] = (ip[t] - i0[t]) / delta;
    }
    for (size_t t = 0; t < e.nodes.size(); ++t)
    {
        double linear = i0[t];
        for (size_t u = 0; u < e.nodes.size(); ++u)
        {
            s.add(idx(e.nodes[t]), idx(e.nodes[u]), j[t][u]);
            linear -= j[t][u] * v[u];
        }
        s.rhs(idx(e.nodes[t]), -linear);
    }
}

System<double> assemble(const Circuit& c, const Layout& l, const Options& o, Mode mode, const std::vector<double>& x,
                        double t, double h, double sourceScale, const ReactiveState& state)
{
    System<double> s(l.size);
    for (int n = 1; n < c.nodeCount(); ++n)
        s.add(idx(n), idx(n), o.gmin);

    const auto& parts = c.elements();
    for (size_t ei = 0; ei < parts.size(); ++ei)
    {
        const auto& e = parts[ei];
        const auto k = l.branch[ei];
        switch (e.type)
        {
            case Element::Type::Resistor:
                s.conductance(e.nodes[0], e.nodes[1], 1.0 / std::max(e.value, 1e-9));
                break;
            case Element::Type::Capacitor:
                if (mode == Mode::Transient)
                {
                    const auto g = 2.0 * e.value / h;
                    s.conductance(e.nodes[0], e.nodes[1], g);
                    const auto ieq = g * state.v[ei] + state.i[ei];
                    s.rhs(idx(e.nodes[0]), ieq);
                    s.rhs(idx(e.nodes[1]), -ieq);
                }
                break;
            case Element::Type::Inductor:
            {
                s.branchIncidence(e.nodes[0], e.nodes[1], k);
                s.add(k, idx(e.nodes[0]), 1.0);
                s.add(k, idx(e.nodes[1]), -1.0);
                if (mode == Mode::Transient)
                {
                    double rhs = -state.v[ei] - (2.0 * e.value / h) * state.i[ei];
                    s.add(k, k, -2.0 * e.value / h);
                    for (const auto& [other, m] : l.mutual[ei])
                    {
                        s.add(k, l.branch[(size_t)other], -2.0 * m / h);
                        rhs -= (2.0 * m / h) * state.i[(size_t)other];
                    }
                    s.rhs(k, rhs);
                }
                break;
            }
            case Element::Type::Coupling:
                break;
            case Element::Type::VoltageSource:
            {
                s.branchIncidence(e.nodes[0], e.nodes[1], k);
                s.add(k, idx(e.nodes[0]), 1.0);
                s.add(k, idx(e.nodes[1]), -1.0);
                const auto value = mode == Mode::Transient ? e.wave.valueAt(t) : e.wave.dcValue();
                s.rhs(k, value * sourceScale);
                break;
            }
            case Element::Type::CurrentSource:
            {
                const auto value = (mode == Mode::Transient ? e.wave.valueAt(t) : e.wave.dcValue()) * sourceScale;
                s.rhs(idx(e.nodes[0]), -value);
                s.rhs(idx(e.nodes[1]), value);
                break;
            }
            case Element::Type::Vcvs:
                s.branchIncidence(e.nodes[0], e.nodes[1], k);
                s.add(k, idx(e.nodes[0]), 1.0);
                s.add(k, idx(e.nodes[1]), -1.0);
                s.add(k, idx(e.nodes[2]), -e.value);
                s.add(k, idx(e.nodes[3]), e.value);
                break;
            case Element::Type::Vccs:
                s.add(idx(e.nodes[0]), idx(e.nodes[2]), e.value);
                s.add(idx(e.nodes[0]), idx(e.nodes[3]), -e.value);
                s.add(idx(e.nodes[1]), idx(e.nodes[2]), -e.value);
                s.add(idx(e.nodes[1]), idx(e.nodes[3]), e.value);
                break;
            case Element::Type::Ccvs:
                s.branchIncidence(e.nodes[0], e.nodes[1], k);
                s.add(k, idx(e.nodes[0]), 1.0);
                s.add(k, idx(e.nodes[1]), -1.0);
                if (e.control >= 0) s.add(k, l.branch[(size_t)e.control], -e.value);
                break;
            case Element::Type::Cccs:
                if (e.control >= 0)
                {
                    s.add(idx(e.nodes[0]), l.branch[(size_t)e.control], e.value);
                    s.add(idx(e.nodes[1]), l.branch[(size_t)e.control], -e.value);
                }
                break;
            case Element::Type::OpAmp:
            {
                // nodes: in+, in-, out, rail+, rail-; output is a voltage source to ground.
                s.add(idx(e.nodes[2]), k, 1.0);
                const auto vin = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
                const auto tf = opampTransfer(e, vin, nodeVoltage(x, e.nodes[3]), nodeVoltage(x, e.nodes[4]));
                s.add(k, idx(e.nodes[2]), 1.0);
                s.add(k, idx(e.nodes[0]), -tf.slope);
                s.add(k, idx(e.nodes[1]), tf.slope);
                s.rhs(k, tf.value - tf.slope * vin);
                break;
            }
            default:
                stampNonlinear(s, e, x);
                break;
        }
    }
    return s;
}

bool newton(const Circuit& c, const Layout& l, const Options& o, Mode mode, std::vector<double>& x, double t, double h,
            double sourceScale, const ReactiveState& state, int& iterations, std::string& error)
{
    for (int it = 0; it < o.maxIterations; ++it)
    {
        auto system = assemble(c, l, o, mode, x, t, h, sourceScale, state);
        std::vector<double> next;
        if (!solveDense(system.a, system.b, next))
        {
            error = "Singular circuit matrix (a floating node or a loop of voltage sources).";
            return false;
        }
        bool converged = true;
        for (size_t i = 0; i < next.size(); ++i)
        {
            auto delta = next[i] - x[i];
            if (l.nonlinear && (int)i < l.nodeUnknowns)
                delta = std::clamp(delta, -o.maxStepVolts * 4.0, o.maxStepVolts * 4.0);
            if (std::abs(delta) > o.absTol + o.relTol * std::abs(next[i]) + ((int)i < l.nodeUnknowns ? 1e-6 : 1e-9))
                converged = false;
            next[i] = x[i] + delta;
        }
        x = next;
        iterations = it + 1;
        if (converged && (it > 0 || !l.nonlinear))
            return true;
        if (!l.nonlinear)
            return true;
    }
    error = "Newton iteration did not converge.";
    return false;
}

std::vector<double> sourceCurrentsFrom(const Layout& l, const std::vector<double>& x)
{
    std::vector<double> currents(l.branch.size(), 0.0);
    for (size_t i = 0; i < l.branch.size(); ++i)
        if (l.branch[i] >= 0)
            currents[i] = x[(size_t)l.branch[i]];
    return currents;
}

std::vector<double> nodeVoltagesFrom(const Circuit& c, const std::vector<double>& x)
{
    std::vector<double> v((size_t)c.nodeCount(), 0.0);
    for (int n = 1; n < c.nodeCount(); ++n)
        v[(size_t)n] = x[(size_t)idx(n)];
    return v;
}

bool solveDcState(const Circuit& c, const Layout& l, const Options& o, std::vector<double>& x, int& iterations, std::string& error)
{
    const ReactiveState none { std::vector<double>(c.elements().size(), 0.0), std::vector<double>(c.elements().size(), 0.0) };
    x.assign((size_t)l.size, 0.0);
    if (newton(c, l, o, Mode::Dc, x, 0.0, 1.0, 1.0, none, iterations, error))
        return true;
    if (error.rfind("Singular", 0) == 0)
        return false; // no amount of source stepping fixes a singular circuit

    // Source stepping: ramp all independent sources up from zero.
    x.assign((size_t)l.size, 0.0);
    for (int step = 1; step <= 20; ++step)
    {
        int its = 0;
        if (!newton(c, l, o, Mode::Dc, x, 0.0, 1.0, step / 20.0, none, its, error))
        {
            error = "DC operating point did not converge (source stepping failed at " + std::to_string(step * 5) + "%).";
            return false;
        }
        iterations += its;
    }
    error.clear();
    return true;
}
}

// ---- public API ---------------------------------------------------------------

double Waveform::valueAt(double t) const
{
    switch (kind)
    {
        case Kind::Dc: return offset;
        case Kind::Sine: return offset + amplitude * std::sin(2.0 * pi * frequency * t + phaseDegrees * pi / 180.0);
        case Kind::Square:
        {
            if (frequency <= 0.0) return offset + amplitude;
            const auto phase = std::fmod(t * frequency + phaseDegrees / 360.0, 1.0);
            return offset + (phase < duty ? amplitude : -amplitude);
        }
    }
    return offset;
}

Node Circuit::addNode() { return nodes++; }

int Circuit::add(Element e)
{
    parts.push_back(std::move(e));
    return (int)parts.size() - 1;
}

int Circuit::addResistor(const std::string& n, Node a, Node b, double ohms) { Element e; e.type = Element::Type::Resistor; e.name = n; e.nodes = { a, b }; e.value = ohms; return add(e); }
int Circuit::addCapacitor(const std::string& n, Node a, Node b, double f) { Element e; e.type = Element::Type::Capacitor; e.name = n; e.nodes = { a, b }; e.value = f; return add(e); }
int Circuit::addInductor(const std::string& n, Node a, Node b, double h) { Element e; e.type = Element::Type::Inductor; e.name = n; e.nodes = { a, b }; e.value = h; return add(e); }
int Circuit::addCoupling(const std::string& n, int la, int lb, double k) { Element e; e.type = Element::Type::Coupling; e.name = n; e.control = la; e.control2 = lb; e.value = k; return add(e); }
int Circuit::addVoltageSource(const std::string& n, Node p, Node m, Waveform w) { Element e; e.type = Element::Type::VoltageSource; e.name = n; e.nodes = { p, m }; e.wave = w; return add(e); }
int Circuit::addCurrentSource(const std::string& n, Node f, Node t, Waveform w) { Element e; e.type = Element::Type::CurrentSource; e.name = n; e.nodes = { f, t }; e.wave = w; return add(e); }
int Circuit::addVcvs(const std::string& n, Node op, Node om, Node cp, Node cm, double g) { Element e; e.type = Element::Type::Vcvs; e.name = n; e.nodes = { op, om, cp, cm }; e.value = g; return add(e); }
int Circuit::addVccs(const std::string& n, Node of, Node ot, Node cp, Node cm, double g) { Element e; e.type = Element::Type::Vccs; e.name = n; e.nodes = { of, ot, cp, cm }; e.value = g; return add(e); }
int Circuit::addCcvs(const std::string& n, Node op, Node om, int src, double g) { Element e; e.type = Element::Type::Ccvs; e.name = n; e.nodes = { op, om }; e.control = src; e.value = g; return add(e); }
int Circuit::addCccs(const std::string& n, Node of, Node ot, int src, double g) { Element e; e.type = Element::Type::Cccs; e.name = n; e.nodes = { of, ot }; e.control = src; e.value = g; return add(e); }
int Circuit::addDiode(const std::string& n, Node a, Node k, DiodeModel m) { Element e; e.type = Element::Type::Diode; e.name = n; e.nodes = { a, k }; e.diode = m; return add(e); }
int Circuit::addBjt(const std::string& n, bool npn, Node c, Node b, Node em, BjtModel m) { Element e; e.type = npn ? Element::Type::Npn : Element::Type::Pnp; e.name = n; e.nodes = { c, b, em }; e.bjt = m; return add(e); }
int Circuit::addMosfet(const std::string& n, bool nc, Node d, Node g, Node s, MosModel m) { Element e; e.type = nc ? Element::Type::Nmos : Element::Type::Pmos; e.name = n; e.nodes = { d, g, s }; e.mos = m; return add(e); }
int Circuit::addOpAmp(const std::string& n, Node ip, Node im, Node out, Node rp, Node rm, OpAmpModel m) { Element e; e.type = Element::Type::OpAmp; e.name = n; e.nodes = { ip, im, out, rp, rm }; e.opamp = m; return add(e); }

OperatingPoint solveOperatingPoint(const Circuit& circuit, const Options& options)
{
    OperatingPoint result;
    const auto layout = makeLayout(circuit);
    if (layout.size == 0)
    {
        result.ok = true;
        result.voltages.assign((size_t)circuit.nodeCount(), 0.0);
        result.sourceCurrents.assign(circuit.elements().size(), 0.0);
        return result;
    }
    std::vector<double> x;
    if (!solveDcState(circuit, layout, options, x, result.iterations, result.error))
        return result;
    result.ok = true;
    result.voltages = nodeVoltagesFrom(circuit, x);
    result.sourceCurrents = sourceCurrentsFrom(layout, x);
    return result;
}

TransientResult solveTransient(const Circuit& circuit, double stopTime, double timeStep, const Options& options, int maxSamples)
{
    TransientResult result;
    if (stopTime <= 0.0 || timeStep <= 0.0)
    {
        result.error = "Transient needs a positive stop time and step.";
        return result;
    }
    const auto layout = makeLayout(circuit);
    const auto& parts = circuit.elements();
    std::vector<double> x;
    int iterations = 0;
    if (!solveDcState(circuit, layout, options, x, iterations, result.error))
        return result;

    // Initial state from the operating point at t = 0 (sources at their t = 0 value).
    {
        std::string error;
        const ReactiveState none { std::vector<double>(parts.size(), 0.0), std::vector<double>(parts.size(), 0.0) };
        Circuit atZero = circuit;
        for (auto& e : atZero.elements())
            if (e.type == Element::Type::VoltageSource || e.type == Element::Type::CurrentSource)
            {
                const auto v0 = e.wave.valueAt(0.0);
                e.wave = Waveform {};
                e.wave.offset = v0;
            }
        std::vector<double> x0;
        int its = 0;
        if (solveDcState(atZero, layout, options, x0, its, error))
            x = x0;
    }

    ReactiveState state { std::vector<double>(parts.size(), 0.0), std::vector<double>(parts.size(), 0.0) };
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto& e = parts[i];
        if (e.type == Element::Type::Capacitor)
            state.v[i] = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
        if (e.type == Element::Type::Inductor)
            state.i[i] = x[(size_t)layout.branch[i]];
    }

    const auto steps = (int)std::ceil(stopTime / timeStep);
    const auto keepEvery = std::max(1, steps / std::max(1, maxSamples));
    auto record = [&](double t) {
        result.time.push_back(t);
        result.voltages.push_back(nodeVoltagesFrom(circuit, x));
        result.sourceCurrents.push_back(sourceCurrentsFrom(layout, x));
    };
    record(0.0);

    for (int n = 1; n <= steps; ++n)
    {
        const auto t = n * timeStep;
        int its = 0;
        if (!newton(circuit, layout, options, Mode::Transient, x, t, timeStep, 1.0, state, its, result.error))
        {
            result.error = "Transient failed at t = " + formatValue(t, "s") + ": " + result.error;
            return result;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto& e = parts[i];
            if (e.type == Element::Type::Capacitor)
            {
                const auto v = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
                const auto g = 2.0 * e.value / timeStep;
                state.i[i] = g * (v - state.v[i]) - state.i[i];
                state.v[i] = v;
            }
            if (e.type == Element::Type::Inductor)
            {
                state.i[i] = x[(size_t)layout.branch[i]];
                state.v[i] = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
            }
        }
        if (n % keepEvery == 0 || n == steps)
            record(t);
    }
    result.ok = true;
    return result;
}

AcResult solveAc(const Circuit& circuit, double startHz, double stopHz, int pointsPerDecade, const Options& options)
{
    AcResult result;
    const auto layout = makeLayout(circuit);
    std::vector<double> op;
    int iterations = 0;
    if (!solveDcState(circuit, layout, options, op, iterations, result.error))
        return result;
    if (startHz <= 0.0 || stopHz <= startHz || pointsPerDecade < 1)
    {
        result.error = "AC sweep needs 0 < start < stop and at least one point per decade.";
        return result;
    }

    const auto& parts = circuit.elements();
    const auto decades = std::log10(stopHz / startHz);
    const auto points = std::max(2, (int)std::ceil(decades * pointsPerDecade) + 1);
    for (int p = 0; p < points; ++p)
    {
        const auto f = startHz * std::pow(10.0, decades * p / (points - 1));
        const auto w = 2.0 * pi * f;
        const std::complex<double> j(0.0, 1.0);
        System<std::complex<double>> s(layout.size);
        for (int n = 1; n < circuit.nodeCount(); ++n)
            s.add(idx(n), idx(n), options.gmin);

        for (size_t ei = 0; ei < parts.size(); ++ei)
        {
            const auto& e = parts[ei];
            const auto k = layout.branch[ei];
            switch (e.type)
            {
                case Element::Type::Resistor: s.conductance(e.nodes[0], e.nodes[1], 1.0 / std::max(e.value, 1e-9)); break;
                case Element::Type::Capacitor: s.conductance(e.nodes[0], e.nodes[1], j * w * e.value); break;
                case Element::Type::Inductor:
                    s.branchIncidence(e.nodes[0], e.nodes[1], k);
                    s.add(k, idx(e.nodes[0]), 1.0);
                    s.add(k, idx(e.nodes[1]), -1.0);
                    s.add(k, k, -j * w * e.value);
                    for (const auto& [other, m] : layout.mutual[ei])
                        s.add(k, layout.branch[(size_t)other], -j * w * m);
                    break;
                case Element::Type::VoltageSource:
                    s.branchIncidence(e.nodes[0], e.nodes[1], k);
                    s.add(k, idx(e.nodes[0]), 1.0);
                    s.add(k, idx(e.nodes[1]), -1.0);
                    s.rhs(k, e.wave.acMagnitude);
                    break;
                case Element::Type::CurrentSource:
                    s.rhs(idx(e.nodes[0]), -e.wave.acMagnitude);
                    s.rhs(idx(e.nodes[1]), e.wave.acMagnitude);
                    break;
                case Element::Type::Vcvs:
                    s.branchIncidence(e.nodes[0], e.nodes[1], k);
                    s.add(k, idx(e.nodes[0]), 1.0); s.add(k, idx(e.nodes[1]), -1.0);
                    s.add(k, idx(e.nodes[2]), -e.value); s.add(k, idx(e.nodes[3]), e.value);
                    break;
                case Element::Type::Vccs:
                    s.add(idx(e.nodes[0]), idx(e.nodes[2]), e.value); s.add(idx(e.nodes[0]), idx(e.nodes[3]), -e.value);
                    s.add(idx(e.nodes[1]), idx(e.nodes[2]), -e.value); s.add(idx(e.nodes[1]), idx(e.nodes[3]), e.value);
                    break;
                case Element::Type::Ccvs:
                    s.branchIncidence(e.nodes[0], e.nodes[1], k);
                    s.add(k, idx(e.nodes[0]), 1.0); s.add(k, idx(e.nodes[1]), -1.0);
                    if (e.control >= 0) s.add(k, layout.branch[(size_t)e.control], -e.value);
                    break;
                case Element::Type::Cccs:
                    if (e.control >= 0)
                    {
                        s.add(idx(e.nodes[0]), layout.branch[(size_t)e.control], e.value);
                        s.add(idx(e.nodes[1]), layout.branch[(size_t)e.control], -e.value);
                    }
                    break;
                case Element::Type::OpAmp:
                {
                    s.add(idx(e.nodes[2]), k, 1.0);
                    const auto vin = nodeVoltage(op, e.nodes[0]) - nodeVoltage(op, e.nodes[1]);
                    const auto tf = opampTransfer(e, vin, nodeVoltage(op, e.nodes[3]), nodeVoltage(op, e.nodes[4]));
                    s.add(k, idx(e.nodes[2]), 1.0);
                    s.add(k, idx(e.nodes[0]), -tf.slope);
                    s.add(k, idx(e.nodes[1]), tf.slope);
                    break;
                }
                case Element::Type::Coupling:
                    break;
                default:
                {
                    // Linearised conductances of nonlinear devices at the operating point.
                    System<double> lin(layout.size);
                    stampNonlinear(lin, e, op);
                    for (size_t r = 0; r < lin.a.size(); ++r)
                        for (size_t col = 0; col < lin.a.size(); ++col)
                            if (lin.a[r][col] != 0.0)
                                s.a[r][col] += lin.a[r][col];
                    break;
                }
            }
        }
        std::vector<std::complex<double>> x;
        if (!solveDense(s.a, s.b, x))
        {
            result.error = "Singular AC matrix at " + formatValue(f, "Hz") + ".";
            return result;
        }
        std::vector<std::complex<double>> v((size_t)circuit.nodeCount(), 0.0);
        for (int n = 1; n < circuit.nodeCount(); ++n)
            v[(size_t)n] = x[(size_t)idx(n)];
        result.frequency.push_back(f);
        result.voltages.push_back(v);
    }
    result.ok = true;
    return result;
}

namespace
{
bool capitalMIsMilli = false;
}

void setCapitalMIsMilli(bool milli)
{
    capitalMIsMilli = milli;
}

bool parseValue(const std::string& rawText, double& out)
{
    std::string text;
    for (auto ch : rawText)
        if (!std::isspace((unsigned char)ch))
            text += ch;
    if (text.empty())
        return false;

    size_t pos = 0;
    double number = 0.0;
    try
    {
        number = std::stod(text, &pos);
    }
    catch (...)
    {
        return false;
    }

    auto rest = text.substr(pos);
    double multiplier = 1.0;
    auto lower = rest;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    size_t used = 0;
    if (lower.rfind("meg", 0) == 0) { multiplier = 1e6; used = 3; }
    else if (!rest.empty())
    {
        switch (rest[0])
        {
            case 'T': multiplier = 1e12; used = 1; break;
            case 'G': multiplier = 1e9; used = 1; break;
            case 'M': multiplier = capitalMIsMilli ? 1e-3 : 1e6; used = 1; break;
            case 'k': case 'K': multiplier = 1e3; used = 1; break;
            case 'm': multiplier = 1e-3; used = 1; break;
            case 'u': multiplier = 1e-6; used = 1; break;
            case 'n': multiplier = 1e-9; used = 1; break;
            case 'p': multiplier = 1e-12; used = 1; break;
            case 'f': multiplier = 1e-15; used = 1; break;
            default: break;
        }
        if (used == 0 && rest.size() >= 2 && (unsigned char)rest[0] == 0xC2 && (unsigned char)rest[1] == 0xB5) { multiplier = 1e-6; used = 2; } // micro sign
    }
    rest = rest.substr(used);

    // European style 4k7 = 4.7k
    if (used > 0 && !rest.empty() && std::isdigit((unsigned char)rest[0]))
    {
        size_t digits = 0;
        while (digits < rest.size() && std::isdigit((unsigned char)rest[digits])) ++digits;
        number += std::stod("0." + rest.substr(0, digits));
        rest = rest.substr(digits);
    }

    // Remaining characters must be a unit name.
    for (auto ch : rest)
        if (!std::isalpha((unsigned char)ch) && (unsigned char)ch < 0x80)
            return false;
    out = number * multiplier;
    return std::isfinite(out);
}

std::string formatValue(double value, const std::string& unit, int significant)
{
    if (value == 0.0 || !std::isfinite(value))
        return "0 " + unit;
    static const std::pair<double, const char*> prefixes[] = {
        { 1e12, "T" }, { 1e9, "G" }, { 1e6, "M" }, { 1e3, "k" }, { 1.0, "" },
        { 1e-3, "m" }, { 1e-6, "u" }, { 1e-9, "n" }, { 1e-12, "p" }, { 1e-15, "f" } };
    const auto magnitude = std::abs(value);
    for (const auto& [scale, prefix] : prefixes)
        if (magnitude >= scale * 0.9995 || scale == 1e-15)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*g", significant, value / scale);
            return std::string(buffer) + " " + prefix + unit;
        }
    return std::to_string(value) + " " + unit;
}
}

#include "CircuitSolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>

namespace circuit_sim
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double boltzmann = 1.380649e-23;
constexpr double electronCharge = 1.602176634e-19;
constexpr double kelvinOffset = 273.15;

using Complex = std::complex<double>;
template <typename T>
using Matrix = std::vector<std::vector<T>>;

// ---- dense linear algebra ---------------------------------------------------

template <typename T>
bool solveDense(Matrix<T> a, std::vector<T> b, std::vector<T>& x)
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

// LU factorisation with partial pivoting, reused for many right-hand sides.
template <typename T>
struct Lu
{
    Matrix<T> a;
    std::vector<size_t> perm;
    bool ok = false;

    explicit Lu(Matrix<T> m) : a(std::move(m))
    {
        const auto n = a.size();
        perm.resize(n);
        for (size_t i = 0; i < n; ++i) perm[i] = i;
        double scale = 0.0;
        for (const auto& row : a)
            for (const auto& v : row)
                scale = std::max(scale, std::abs(v));
        for (size_t col = 0; col < n; ++col)
        {
            size_t pivot = col;
            double best = std::abs(a[col][col]);
            for (size_t row = col + 1; row < n; ++row)
                if (std::abs(a[row][col]) > best) { best = std::abs(a[row][col]); pivot = row; }
            if (best <= 1e-13 * std::max(scale, 1e-300))
                return;
            std::swap(a[col], a[pivot]);
            std::swap(perm[col], perm[pivot]);
            for (size_t row = col + 1; row < n; ++row)
            {
                a[row][col] /= a[col][col];
                const auto f = a[row][col];
                if (f == T(0)) continue;
                for (size_t k = col + 1; k < n; ++k)
                    a[row][k] -= f * a[col][k];
            }
        }
        ok = true;
    }

    std::vector<T> solve(const std::vector<T>& b) const
    {
        const auto n = a.size();
        std::vector<T> y(n);
        for (size_t i = 0; i < n; ++i)
        {
            T sum = b[perm[i]];
            for (size_t k = 0; k < i; ++k) sum -= a[i][k] * y[k];
            y[i] = sum;
        }
        for (size_t i = n; i-- > 0;)
        {
            T sum = y[i];
            for (size_t k = i + 1; k < n; ++k) sum -= a[i][k] * y[k];
            y[i] = sum / a[i][i];
        }
        return y;
    }
};

template <typename T>
Matrix<T> transpose(const Matrix<T>& m)
{
    Matrix<T> t(m.empty() ? 0 : m[0].size(), std::vector<T>(m.size()));
    for (size_t i = 0; i < m.size(); ++i)
        for (size_t j = 0; j < m[i].size(); ++j)
            t[j][i] = m[i][j];
    return t;
}

// Eigenvalues of a general complex matrix: Householder reduction to
// Hessenberg form, then single-shift QR with Wilkinson shifts and deflation.
bool eigenvalues(Matrix<Complex> a, std::vector<Complex>& out)
{
    const auto n = (int)a.size();
    out.clear();
    for (int k = 0; k + 2 < n; ++k)
    {
        const int m = n - k - 1;
        std::vector<Complex> v((size_t)m);
        double alpha = 0.0;
        for (int i = 0; i < m; ++i) { v[(size_t)i] = a[(size_t)(k + 1 + i)][(size_t)k]; alpha += std::norm(v[(size_t)i]); }
        alpha = std::sqrt(alpha);
        if (alpha == 0.0)
            continue;
        const auto phase = std::abs(v[0]) > 0.0 ? v[0] / std::abs(v[0]) : Complex(1.0);
        v[0] += phase * alpha;
        double vnorm2 = 0.0;
        for (const auto& x : v) vnorm2 += std::norm(x);
        if (vnorm2 == 0.0)
            continue;
        for (int j = 0; j < n; ++j)
        {
            Complex s = 0.0;
            for (int i = 0; i < m; ++i) s += std::conj(v[(size_t)i]) * a[(size_t)(k + 1 + i)][(size_t)j];
            s *= 2.0 / vnorm2;
            for (int i = 0; i < m; ++i) a[(size_t)(k + 1 + i)][(size_t)j] -= v[(size_t)i] * s;
        }
        for (int i = 0; i < n; ++i)
        {
            Complex s = 0.0;
            for (int j = 0; j < m; ++j) s += a[(size_t)i][(size_t)(k + 1 + j)] * v[(size_t)j];
            s *= 2.0 / vnorm2;
            for (int j = 0; j < m; ++j) a[(size_t)i][(size_t)(k + 1 + j)] -= s * std::conj(v[(size_t)j]);
        }
        for (int i = k + 2; i < n; ++i)
            a[(size_t)i][(size_t)k] = 0.0;
    }

    int hi = n - 1;
    int iterations = 0;
    const int limit = 100 * std::max(1, n);
    std::vector<Complex> c((size_t)std::max(1, n)), s((size_t)std::max(1, n));
    while (hi >= 0)
    {
        if (hi == 0) { out.push_back(a[0][0]); --hi; continue; }
        int l = hi;
        while (l > 0)
        {
            const auto off = std::abs(a[(size_t)l][(size_t)(l - 1)]);
            const auto diag = std::abs(a[(size_t)l][(size_t)l]) + std::abs(a[(size_t)(l - 1)][(size_t)(l - 1)]);
            if (off <= 1e-15 * diag || off < 1e-300) { a[(size_t)l][(size_t)(l - 1)] = 0.0; break; }
            --l;
        }
        if (l == hi) { out.push_back(a[(size_t)hi][(size_t)hi]); --hi; iterations = 0; continue; }
        if (++iterations > limit)
            return false;

        const auto aa = a[(size_t)(hi - 1)][(size_t)(hi - 1)], bb = a[(size_t)(hi - 1)][(size_t)hi];
        const auto cc = a[(size_t)hi][(size_t)(hi - 1)], dd = a[(size_t)hi][(size_t)hi];
        const auto tr = aa + dd, det = aa * dd - bb * cc;
        const auto disc = std::sqrt(tr * tr / 4.0 - det);
        const auto mu1 = tr / 2.0 + disc, mu2 = tr / 2.0 - disc;
        auto shift = std::abs(mu1 - dd) < std::abs(mu2 - dd) ? mu1 : mu2;
        if (iterations % 11 == 10)
            shift = dd + Complex(1.5 * std::abs(cc), 0.0);

        for (int i = l; i <= hi; ++i) a[(size_t)i][(size_t)i] -= shift;
        for (int k = l; k < hi; ++k)
        {
            const auto x = a[(size_t)k][(size_t)k], y = a[(size_t)(k + 1)][(size_t)k];
            const auto r = std::sqrt(std::norm(x) + std::norm(y));
            c[(size_t)k] = r > 0.0 ? x / r : Complex(1.0);
            s[(size_t)k] = r > 0.0 ? y / r : Complex(0.0);
            for (int j = k; j <= hi; ++j)
            {
                const auto t1 = a[(size_t)k][(size_t)j], t2 = a[(size_t)(k + 1)][(size_t)j];
                a[(size_t)k][(size_t)j] = std::conj(c[(size_t)k]) * t1 + std::conj(s[(size_t)k]) * t2;
                a[(size_t)(k + 1)][(size_t)j] = -s[(size_t)k] * t1 + c[(size_t)k] * t2;
            }
        }
        for (int k = l; k < hi; ++k)
            for (int i = l; i <= std::min(k + 2, hi); ++i)
            {
                const auto t1 = a[(size_t)i][(size_t)k], t2 = a[(size_t)i][(size_t)(k + 1)];
                a[(size_t)i][(size_t)k] = t1 * c[(size_t)k] + t2 * s[(size_t)k];
                a[(size_t)i][(size_t)(k + 1)] = -t1 * std::conj(s[(size_t)k]) + t2 * std::conj(c[(size_t)k]);
            }
        for (int i = l; i <= hi; ++i) a[(size_t)i][(size_t)i] += shift;
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
    const auto nvt = m.emission * m.vt;
    auto id = m.saturationCurrent * (safeExp(vd / nvt) - 1.0);
    if (m.breakdownVoltage > 0.0 && vd < -m.breakdownVoltage)
        id -= m.saturationCurrent * (safeExp(-(vd + m.breakdownVoltage) / nvt) - 1.0);
    return id;
}

void npnCurrents(const BjtModel& m, double vbe, double vbc, double& ic, double& ib)
{
    const auto ef = safeExp(vbe / m.vt) - 1.0;
    const auto er = safeExp(vbc / m.vt) - 1.0;
    auto transport = m.saturationCurrent * (ef - er);
    if (m.earlyVoltage > 0.0)
        transport *= std::max(0.05, 1.0 - vbc / m.earlyVoltage);
    ic = transport - m.saturationCurrent / m.betaReverse * er;
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

// d(current into terminal t) / d(voltage at terminal u), numerically.
Matrix<double> deviceJacobian(const Element& e, const std::vector<double>& v)
{
    const auto i0 = deviceCurrents(e, v);
    const double delta = 1e-6;
    Matrix<double> j(v.size(), std::vector<double>(v.size(), 0.0));
    for (size_t u = 0; u < v.size(); ++u)
    {
        auto vp = v;
        vp[u] += delta;
        const auto ip = deviceCurrents(e, vp);
        for (size_t t = 0; t < v.size(); ++t)
            j[t][u] = (ip[t] - i0[t]) / delta;
    }
    return j;
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
    if (!e.opamp.limited)
        return { e.opamp.gain * vin, e.opamp.gain };
    auto high = railPlus - e.opamp.railDrop;
    auto low = railMinus + e.opamp.railDrop;
    if (high - low < 0.2)
    {
        high = 15.0 - e.opamp.railDrop;
        low = -15.0 + e.opamp.railDrop;
    }
    // Linear (A * vin) between the limits, rounded off over ~0.1 V at each:
    // out = v - softplus(v - high) + softplus(low - v). No offset at mid-rail.
    constexpr double sharpness = 40.0; // 1/V
    auto softplus = [](double x) { return x > 30.0 ? x : x < -30.0 ? std::exp(x) : std::log1p(std::exp(x)); };
    auto logistic = [](double x) { return x >= 0.0 ? 1.0 / (1.0 + std::exp(-x)) : std::exp(x) / (1.0 + std::exp(x)); };
    const auto v = e.opamp.gain * vin;
    const auto value = v - softplus(sharpness * (v - high)) / sharpness + softplus(sharpness * (low - v)) / sharpness;
    const auto slope = e.opamp.gain * (1.0 - logistic(sharpness * (v - high)) - logistic(sharpness * (low - v)));
    return { value, std::max(slope, e.opamp.gain * 1e-9) };
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
    Matrix<T> a;
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

std::vector<double> terminalVoltages(const Element& e, const std::vector<double>& x)
{
    std::vector<double> v;
    for (auto n : e.nodes) v.push_back(nodeVoltage(x, n));
    return v;
}

// Junction voltages remembered between Newton iterations, for limiting.
struct JunctionMemory
{
    std::vector<std::vector<double>> voltages; // per element
    bool limited = false;                      // a junction was limited this iteration
};

// SPICE pnjlim: keeps an exponential junction from jumping far up its curve in one step.
double pnjlim(double vnew, double vold, double vt, double vcrit, bool& limited)
{
    if (vnew > vcrit && std::abs(vnew - vold) > 2.0 * vt)
    {
        limited = true;
        if (vold > 0.0)
        {
            const auto arg = 1.0 + (vnew - vold) / vt;
            return arg > 0.0 ? vold + vt * std::log(arg) : vcrit;
        }
        return vt * std::log(vnew / vt);
    }
    return vnew;
}

// Replaces the terminal voltages a device is linearised at with limited ones.
void limitJunctions(const Element& e, std::vector<double>& v, std::vector<double>& memory, bool& limited)
{
    switch (e.type)
    {
        case Element::Type::Diode:
        {
            const auto nvt = e.diode.emission * e.diode.vt;
            const auto vcrit = nvt * std::log(nvt / (std::sqrt(2.0) * e.diode.saturationCurrent));
            auto vd = v[0] - v[1];
            if (!memory.empty())
            {
                if (e.diode.breakdownVoltage > 0.0 && vd < -e.diode.breakdownVoltage + 10.0 * nvt && memory[0] < 0.0)
                {
                    // Reverse breakdown is an exponential too, mirrored about -BV.
                    const auto vr = pnjlim(-(vd + e.diode.breakdownVoltage), -(memory[0] + e.diode.breakdownVoltage), nvt, vcrit, limited);
                    vd = -(vr + e.diode.breakdownVoltage);
                }
                else
                    vd = pnjlim(vd, memory[0], nvt, vcrit, limited);
            }
            memory = { vd };
            v[0] = v[1] + vd;
            break;
        }
        case Element::Type::Npn:
        case Element::Type::Pnp:
        {
            const auto sign = e.type == Element::Type::Npn ? 1.0 : -1.0;
            const auto vt = e.bjt.vt;
            const auto vcrit = vt * std::log(vt / (std::sqrt(2.0) * e.bjt.saturationCurrent));
            auto vbe = sign * (v[1] - v[2]), vbc = sign * (v[1] - v[0]);
            if (memory.size() == 2)
            {
                vbe = pnjlim(vbe, memory[0], vt, vcrit, limited);
                vbc = pnjlim(vbc, memory[1], vt, vcrit, limited);
            }
            memory = { vbe, vbc };
            v[2] = v[1] - sign * vbe;
            v[0] = v[1] - sign * vbc;
            break;
        }
        default:
            break;
    }
}

void stampNonlinear(System<double>& s, const Element& e, const std::vector<double>& x, JunctionMemory* memory = nullptr, size_t index = 0)
{
    auto v = terminalVoltages(e, x);
    if (memory != nullptr)
        limitJunctions(e, v, memory->voltages[index], memory->limited);
    const auto i0 = deviceCurrents(e, v);
    const auto j = deviceJacobian(e, v);
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
                        double t, double h, double sourceScale, const ReactiveState& state, JunctionMemory* memory = nullptr)
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
                stampNonlinear(s, e, x, memory, ei);
                break;
        }
    }
    return s;
}

bool newton(const Circuit& c, const Layout& l, const Options& o, Mode mode, std::vector<double>& x, double t, double h,
            double sourceScale, const ReactiveState& state, int& iterations, std::string& error)
{
    JunctionMemory memory;
    memory.voltages.assign(c.elements().size(), {});
    for (int it = 0; it < o.maxIterations; ++it)
    {
        memory.limited = false;
        auto system = assemble(c, l, o, mode, x, t, h, sourceScale, state, &memory);
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
        if (converged && !memory.limited && (it > 0 || !l.nonlinear))
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

const ReactiveState& noReactiveState(const Circuit& c)
{
    thread_local ReactiveState none;
    none.v.assign(c.elements().size(), 0.0);
    none.i.assign(c.elements().size(), 0.0);
    return none;
}

bool solveDcState(const Circuit& c, const Layout& l, const Options& o, std::vector<double>& x, int& iterations,
                  std::string& error, bool warmStart = false)
{
    const auto& none = noReactiveState(c);
    if (warmStart && (int)x.size() == l.size)
    {
        auto guess = x;
        if (newton(c, l, o, Mode::Dc, guess, 0.0, 1.0, 1.0, none, iterations, error))
        {
            x = guess;
            return true;
        }
    }
    x.assign((size_t)l.size, 0.0);
    if (newton(c, l, o, Mode::Dc, x, 0.0, 1.0, 1.0, none, iterations, error))
        return true;
    if (error.rfind("Singular", 0) == 0)
        return false; // no amount of stepping fixes a singular circuit

    // Gmin stepping: a large conductance from every node to ground, reduced a decade at a time.
    {
        x.assign((size_t)l.size, 0.0);
        bool ok = true;
        for (double g = 1e-2; g > o.gmin * 1.0001 && ok; g /= 10.0)
        {
            auto stepped = o;
            stepped.gmin = g;
            int its = 0;
            ok = newton(c, l, stepped, Mode::Dc, x, 0.0, 1.0, 1.0, none, its, error);
            iterations += its;
        }
        int its = 0;
        if (ok && newton(c, l, o, Mode::Dc, x, 0.0, 1.0, 1.0, none, its, error))
        {
            iterations += its;
            error.clear();
            return true;
        }
    }

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

OperatingPoint operatingPointFrom(const Circuit& c, const Layout& l, const std::vector<double>& x, int iterations)
{
    OperatingPoint op;
    op.ok = true;
    op.voltages = nodeVoltagesFrom(c, x);
    op.sourceCurrents = sourceCurrentsFrom(l, x);
    op.iterations = iterations;
    return op;
}

std::vector<double> stateFrom(const Circuit& c, const Layout& l, const OperatingPoint& op)
{
    std::vector<double> x((size_t)l.size, 0.0);
    if ((int)op.voltages.size() != c.nodeCount() || op.sourceCurrents.size() != c.elements().size())
        return {};
    for (int n = 1; n < c.nodeCount(); ++n)
        x[(size_t)idx(n)] = op.voltages[(size_t)n];
    for (size_t i = 0; i < l.branch.size(); ++i)
        if (l.branch[i] >= 0)
            x[(size_t)l.branch[i]] = op.sourceCurrents[i];
    return x;
}

// ---- small-signal model ------------------------------------------------------

// Y(s) = G + sC linearised at the operating point x.
struct SmallSignal
{
    Layout layout;
    std::vector<double> x;
    Matrix<double> g, c;
    OperatingPoint op;
};

bool linearize(const Circuit& circuit, const Options& o, SmallSignal& ss, std::string& error, const std::vector<double>* guess = nullptr)
{
    ss.layout = makeLayout(circuit);
    const auto& l = ss.layout;
    int iterations = 0;
    if (guess != nullptr) ss.x = *guess;
    if (!solveDcState(circuit, l, o, ss.x, iterations, error, guess != nullptr))
        return false;
    ss.op = operatingPointFrom(circuit, l, ss.x, iterations);
    ss.g = assemble(circuit, l, o, Mode::Dc, ss.x, 0.0, 1.0, 1.0, noReactiveState(circuit)).a;
    ss.c.assign((size_t)l.size, std::vector<double>((size_t)l.size, 0.0));

    auto capacitance = [&](Node p, Node q, double cap) {
        auto add = [&](int r, int col, double v) { if (r >= 0 && col >= 0) ss.c[(size_t)r][(size_t)col] += v; };
        add(idx(p), idx(p), cap); add(idx(q), idx(q), cap);
        add(idx(p), idx(q), -cap); add(idx(q), idx(p), -cap);
    };
    const auto& parts = circuit.elements();
    for (size_t ei = 0; ei < parts.size(); ++ei)
    {
        const auto& e = parts[ei];
        const auto k = l.branch[ei];
        switch (e.type)
        {
            case Element::Type::Capacitor:
                capacitance(e.nodes[0], e.nodes[1], e.value);
                break;
            case Element::Type::Inductor:
                ss.c[(size_t)k][(size_t)k] -= e.value;
                for (const auto& [other, m] : l.mutual[ei])
                    ss.c[(size_t)k][(size_t)l.branch[(size_t)other]] -= m;
                break;
            case Element::Type::Diode:
                if (e.diode.transitTime > 0.0)
                {
                    const auto j = deviceJacobian(e, terminalVoltages(e, ss.x));
                    capacitance(e.nodes[0], e.nodes[1], e.diode.transitTime * std::abs(j[0][0]));
                }
                break;
            case Element::Type::Npn:
            case Element::Type::Pnp:
                if (e.bjt.transitTime > 0.0)
                {
                    // gm = d(Ic)/d(Vb) with the emitter fixed.
                    const auto j = deviceJacobian(e, terminalVoltages(e, ss.x));
                    capacitance(e.nodes[1], e.nodes[2], e.bjt.transitTime * std::abs(j[0][1]));
                }
                break;
            default:
                break;
        }
    }
    return true;
}

std::vector<Complex> acExcitation(const Circuit& circuit, const Layout& l)
{
    std::vector<Complex> b((size_t)l.size, 0.0);
    const auto& parts = circuit.elements();
    for (size_t ei = 0; ei < parts.size(); ++ei)
    {
        const auto& e = parts[ei];
        const auto ac = std::polar(e.wave.acMagnitude, e.wave.acPhaseDegrees * pi / 180.0);
        if (e.type == Element::Type::VoltageSource)
            b[(size_t)l.branch[ei]] += ac;
        else if (e.type == Element::Type::CurrentSource)
        {
            if (e.nodes[0] != 0) b[(size_t)idx(e.nodes[0])] -= ac;
            if (e.nodes[1] != 0) b[(size_t)idx(e.nodes[1])] += ac;
        }
    }
    return b;
}

Matrix<Complex> admittance(const SmallSignal& ss, double w)
{
    const auto n = ss.g.size();
    Matrix<Complex> y(n, std::vector<Complex>(n));
    for (size_t r = 0; r < n; ++r)
        for (size_t col = 0; col < n; ++col)
            y[r][col] = Complex(ss.g[r][col], w * ss.c[r][col]);
    return y;
}

// The circuit with only `inputSource` driving the AC analysis (1 V or 1 A, 0 deg).
Circuit withAcInput(const Circuit& circuit, int inputSource)
{
    auto copy = circuit;
    for (size_t i = 0; i < copy.elements().size(); ++i)
    {
        auto& w = copy.elements()[i].wave;
        w.acMagnitude = (int)i == inputSource ? 1.0 : 0.0;
        w.acPhaseDegrees = 0.0;
    }
    return copy;
}

bool isIndependentSource(const Circuit& c, int element)
{
    return element >= 0 && element < (int)c.elements().size()
        && (c.elements()[(size_t)element].type == Element::Type::VoltageSource
            || c.elements()[(size_t)element].type == Element::Type::CurrentSource);
}

std::vector<double> logFrequencies(double startHz, double stopHz, int pointsPerDecade)
{
    std::vector<double> f;
    if (startHz <= 0.0 || stopHz <= startHz || pointsPerDecade < 1)
        return f;
    const auto decades = std::log10(stopHz / startHz);
    const auto points = std::max(2, (int)std::ceil(decades * pointsPerDecade) + 1);
    for (int p = 0; p < points; ++p)
        f.push_back(startHz * std::pow(10.0, decades * p / (points - 1)));
    return f;
}

Complex outputOf(const std::vector<Complex>& x, Node plus, Node minus)
{
    auto v = [&](Node n) { return n == 0 ? Complex(0.0) : x[(size_t)idx(n)]; };
    return v(plus) - v(minus);
}

double outputOf(const std::vector<double>& voltages, Node plus, Node minus)
{
    return voltages[(size_t)plus] - voltages[(size_t)minus];
}

double kelvin(double celsius) { return celsius + kelvinOffset; }
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
        case Kind::Pulse:
        {
            if (t < delay) return offset;
            auto local = t - delay;
            if (period > 0.0) local = std::fmod(local, period);
            if (local < rise) return offset + (pulsed - offset) * local / rise;
            if (local < rise + width) return pulsed;
            if (local < rise + width + fall) return pulsed - (pulsed - offset) * (local - rise - width) / fall;
            return offset;
        }
        case Kind::Pwl:
        {
            if (points.empty()) return offset;
            if (t <= points.front().first) return points.front().second;
            for (size_t i = 1; i < points.size(); ++i)
                if (t <= points[i].first)
                {
                    const auto span = points[i].first - points[i - 1].first;
                    const auto f = span > 0.0 ? (t - points[i - 1].first) / span : 1.0;
                    return points[i - 1].second + f * (points[i].second - points[i - 1].second);
                }
            return points.back().second;
        }
        case Kind::Exp:
        {
            auto v = offset;
            if (t >= delay && tau1 > 0.0) v += (pulsed - offset) * (1.0 - std::exp(-(t - delay) / tau1));
            if (t >= delay2 && delay2 > delay && tau2 > 0.0) v += (offset - pulsed) * (1.0 - std::exp(-(t - delay2) / tau2));
            return v;
        }
    }
    return offset;
}

double Waveform::dcValue() const
{
    switch (kind)
    {
        case Kind::Pulse: case Kind::Pwl: case Kind::Exp: return valueAt(0.0);
        default: return offset;
    }
}

std::vector<double> Waveform::breakpoints(double stop) const
{
    std::vector<double> corners;
    auto add = [&](double t) { if (t > 0.0 && t < stop) corners.push_back(t); };
    switch (kind)
    {
        case Kind::Pulse:
        {
            const auto cycle = period > 0.0 ? period : stop + 1.0;
            for (double base = delay; base < stop && corners.size() < 40000; base += cycle)
            {
                add(base); add(base + rise); add(base + rise + width); add(base + rise + width + fall);
                if (period <= 0.0) break;
            }
            break;
        }
        case Kind::Pwl:
            for (const auto& p : points) add(p.first);
            break;
        case Kind::Exp:
            add(delay); add(delay2);
            break;
        case Kind::Square:
            if (frequency > 0.0)
            {
                const auto t0 = -phaseDegrees / 360.0 / frequency;
                for (int k = 0; k < 40000; ++k)
                {
                    const auto base = t0 + k / frequency;
                    if (base > stop) break;
                    add(base); add(base + duty / frequency);
                }
            }
            break;
        default:
            break;
    }
    return corners;
}

Node Circuit::addNode() { return nodes++; }

int Circuit::add(Element e)
{
    parts.push_back(std::move(e));
    return (int)parts.size() - 1;
}

int Circuit::find(const std::string& name) const
{
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].name == name)
            return (int)i;
    return -1;
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

// ---- element parameters ---------------------------------------------------------

std::vector<std::string> parameterNames(Element::Type type)
{
    using T = Element::Type;
    switch (type)
    {
        case T::Resistor: return { "value", "tc1", "tc2" };
        case T::Capacitor: case T::Inductor: case T::Coupling: return { "value" };
        case T::VoltageSource: case T::CurrentSource: return { "dc", "amplitude", "frequency", "ac" };
        case T::Vcvs: case T::Vccs: case T::Ccvs: case T::Cccs: return { "value" };
        case T::Diode: return { "is", "n", "bv", "tt" };
        case T::Npn: case T::Pnp: return { "beta", "is", "vaf", "tf" };
        case T::Nmos: case T::Pmos: return { "vth", "k", "lambda" };
        case T::OpAmp: return { "gain" };
    }
    return {};
}

bool getParameter(const Element& e, const std::string& name, double& out)
{
    using T = Element::Type;
    switch (e.type)
    {
        case T::Resistor:
            if (name == "tc1") { out = e.tc1; return true; }
            if (name == "tc2") { out = e.tc2; return true; }
            [[fallthrough]];
        case T::Capacitor: case T::Inductor: case T::Coupling:
        case T::Vcvs: case T::Vccs: case T::Ccvs: case T::Cccs:
            if (name == "value") { out = e.value; return true; }
            return false;
        case T::VoltageSource: case T::CurrentSource:
            if (name == "dc") { out = e.wave.kind == Waveform::Kind::Dc || e.wave.kind == Waveform::Kind::Sine || e.wave.kind == Waveform::Kind::Square ? e.wave.offset : e.wave.dcValue(); return true; }
            if (name == "amplitude") { out = e.wave.amplitude; return true; }
            if (name == "frequency") { out = e.wave.frequency; return true; }
            if (name == "ac") { out = e.wave.acMagnitude; return true; }
            return false;
        case T::Diode:
            if (name == "is") { out = e.diode.saturationCurrent; return true; }
            if (name == "n") { out = e.diode.emission; return true; }
            if (name == "bv") { out = e.diode.breakdownVoltage; return true; }
            if (name == "tt") { out = e.diode.transitTime; return true; }
            return false;
        case T::Npn: case T::Pnp:
            if (name == "beta") { out = e.bjt.betaForward; return true; }
            if (name == "is") { out = e.bjt.saturationCurrent; return true; }
            if (name == "vaf") { out = e.bjt.earlyVoltage; return true; }
            if (name == "tf") { out = e.bjt.transitTime; return true; }
            return false;
        case T::Nmos: case T::Pmos:
            if (name == "vth") { out = e.mos.threshold; return true; }
            if (name == "k") { out = e.mos.transconductance; return true; }
            if (name == "lambda") { out = e.mos.lambda; return true; }
            return false;
        case T::OpAmp:
            if (name == "gain") { out = e.opamp.gain; return true; }
            return false;
    }
    return false;
}

bool setParameter(Element& e, const std::string& name, double value)
{
    using T = Element::Type;
    double unused = 0.0;
    if (!getParameter(e, name, unused))
        return false;
    switch (e.type)
    {
        case T::Resistor:
            if (name == "tc1") { e.tc1 = value; return true; }
            if (name == "tc2") { e.tc2 = value; return true; }
            e.value = value;
            return true;
        case T::Capacitor: case T::Inductor: case T::Coupling:
        case T::Vcvs: case T::Vccs: case T::Ccvs: case T::Cccs:
            e.value = value;
            return true;
        case T::VoltageSource: case T::CurrentSource:
            if (name == "dc")
            {
                if (e.wave.kind == Waveform::Kind::Pulse || e.wave.kind == Waveform::Kind::Exp)
                {
                    const auto shift = value - e.wave.offset;
                    e.wave.offset += shift;
                    e.wave.pulsed += shift;
                }
                else if (e.wave.kind == Waveform::Kind::Pwl)
                {
                    const auto shift = value - e.wave.dcValue();
                    for (auto& p : e.wave.points) p.second += shift;
                }
                else
                    e.wave.offset = value;
            }
            if (name == "amplitude") e.wave.amplitude = value;
            if (name == "frequency") e.wave.frequency = value;
            if (name == "ac") e.wave.acMagnitude = value;
            return true;
        case T::Diode:
            if (name == "is") e.diode.saturationCurrent = value;
            if (name == "n") e.diode.emission = value;
            if (name == "bv") e.diode.breakdownVoltage = value;
            if (name == "tt") e.diode.transitTime = value;
            return true;
        case T::Npn: case T::Pnp:
            if (name == "beta") e.bjt.betaForward = value;
            if (name == "is") e.bjt.saturationCurrent = value;
            if (name == "vaf") e.bjt.earlyVoltage = value;
            if (name == "tf") e.bjt.transitTime = value;
            return true;
        case T::Nmos: case T::Pmos:
            if (name == "vth") e.mos.threshold = value;
            if (name == "k") e.mos.transconductance = value;
            if (name == "lambda") e.mos.lambda = value;
            return true;
        case T::OpAmp:
            e.opamp.gain = value;
            return true;
    }
    return false;
}

Circuit atTemperature(const Circuit& circuit, double celsius)
{
    auto copy = circuit;
    if (std::abs(celsius - nominalTemperatureC) < 1e-12)
        return copy;
    const auto t = kelvin(celsius), tn = kelvin(nominalTemperatureC);
    const auto dt = celsius - nominalTemperatureC;
    const auto vt = boltzmann * t / electronCharge;
    constexpr double bandgap = 1.11, xti = 3.0;
    for (auto& e : copy.elements())
    {
        switch (e.type)
        {
            case Element::Type::Resistor:
                e.value *= std::max(1e-6, 1.0 + e.tc1 * dt + e.tc2 * dt * dt);
                break;
            case Element::Type::Diode:
            {
                const auto n = e.diode.emission;
                e.diode.saturationCurrent *= std::pow(t / tn, xti / n) * std::exp(bandgap / (n * vt) * (t / tn - 1.0));
                e.diode.vt = vt;
                break;
            }
            case Element::Type::Npn:
            case Element::Type::Pnp:
                e.bjt.saturationCurrent *= std::pow(t / tn, xti) * std::exp(bandgap / vt * (t / tn - 1.0));
                e.bjt.vt = vt;
                break;
            case Element::Type::Nmos:
            case Element::Type::Pmos:
                e.mos.threshold -= 2e-3 * dt;
                e.mos.transconductance *= std::pow(t / tn, -1.5);
                break;
            default:
                break;
        }
    }
    return copy;
}

// ---- operating point and device detail -------------------------------------------

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
    return operatingPointFrom(circuit, layout, x, result.iterations);
}

OperatingPoint solveOperatingPointFrom(const Circuit& circuit, const OperatingPoint& guess, const Options& options)
{
    const auto layout = makeLayout(circuit);
    auto x = guess.ok ? stateFrom(circuit, layout, guess) : std::vector<double>();
    OperatingPoint result;
    if (layout.size == 0)
        return solveOperatingPoint(circuit, options);
    if (!solveDcState(circuit, layout, options, x, result.iterations, result.error, !x.empty()))
        return result;
    return operatingPointFrom(circuit, layout, x, result.iterations);
}

std::vector<double> terminalCurrents(const Circuit& circuit, const OperatingPoint& op, int element)
{
    if (!op.ok || element < 0 || element >= (int)circuit.elements().size())
        return {};
    const auto& e = circuit.elements()[(size_t)element];
    auto v = [&](size_t terminal) { return op.voltages[(size_t)e.nodes[terminal]]; };
    using T = Element::Type;
    switch (e.type)
    {
        case T::Resistor:
        {
            const auto i = (v(0) - v(1)) / std::max(e.value, 1e-9);
            return { i, -i };
        }
        case T::Capacitor: return { 0.0, 0.0 };
        case T::Inductor: case T::VoltageSource: case T::Vcvs: case T::Ccvs:
        {
            const auto i = op.sourceCurrents[(size_t)element];
            return e.type == T::Vcvs ? std::vector<double> { i, -i, 0.0, 0.0 } : std::vector<double> { i, -i };
        }
        case T::CurrentSource:
        {
            const auto i = e.wave.dcValue();
            return { i, -i };
        }
        case T::Vccs:
        {
            const auto i = e.value * (v(2) - v(3));
            return { i, -i, 0.0, 0.0 };
        }
        case T::Cccs:
        {
            const auto i = e.control >= 0 ? e.value * op.sourceCurrents[(size_t)e.control] : 0.0;
            return { i, -i };
        }
        case T::OpAmp:
            return { 0.0, 0.0, op.sourceCurrents[(size_t)element], 0.0, 0.0 };
        case T::Coupling:
            return {};
        default:
        {
            std::vector<double> volts;
            for (auto n : e.nodes) volts.push_back(op.voltages[(size_t)n]);
            return deviceCurrents(e, volts);
        }
    }
}

double absorbedPower(const Circuit& circuit, const OperatingPoint& op, int element)
{
    const auto currents = terminalCurrents(circuit, op, element);
    const auto& e = circuit.elements()[(size_t)element];
    double p = 0.0;
    for (size_t t = 0; t < currents.size() && t < e.nodes.size(); ++t)
        p += op.voltages[(size_t)e.nodes[t]] * currents[t];
    return p;
}

DeviceInfo deviceInfo(const Circuit& circuit, const OperatingPoint& op, int element)
{
    DeviceInfo info;
    if (!op.ok || element < 0 || element >= (int)circuit.elements().size())
        return info;
    const auto& e = circuit.elements()[(size_t)element];
    std::vector<double> volts;
    for (auto n : e.nodes) volts.push_back(op.voltages[(size_t)n]);
    auto add = [&](const std::string& name, double value, const std::string& unit) {
        info.values.push_back({ name, value });
        info.units.push_back(unit);
    };
    using T = Element::Type;
    switch (e.type)
    {
        case T::Diode:
        {
            const auto vd = volts[0] - volts[1];
            const auto id = diodeCurrent(e.diode, vd);
            const auto j = deviceJacobian(e, volts);
            const auto gd = j[0][0];
            add("Vd", vd, "V");
            add("Id", id, "A");
            add("gd", gd, "S");
            add("rd", gd > 0.0 ? 1.0 / gd : INFINITY, "ohm");
            if (e.diode.transitTime > 0.0) add("Cd", e.diode.transitTime * gd, "F");
            info.region = e.diode.breakdownVoltage > 0.0 && vd < -e.diode.breakdownVoltage * 0.98 ? "breakdown"
                        : vd > 0.3 * e.diode.emission ? "forward" : "off";
            break;
        }
        case T::Npn:
        case T::Pnp:
        {
            const auto sign = e.type == T::Npn ? 1.0 : -1.0;
            const auto i = deviceCurrents(e, volts);
            const auto j = deviceJacobian(e, volts);
            const auto vbe = volts[1] - volts[2], vbc = volts[1] - volts[0], vce = volts[0] - volts[2];
            // Base perturbed with collector and emitter fixed: d/dVbe at constant Vce.
            const auto gm = std::abs(j[0][1]);
            const auto gpi = std::abs(j[1][1]);
            const auto go = std::abs(j[0][0]);
            add("Vbe", vbe, "V");
            add("Vbc", vbc, "V");
            add("Vce", vce, "V");
            add("Ic", i[0], "A");
            add("Ib", i[1], "A");
            add("Ie", i[2], "A");
            add("beta (Ic/Ib)", std::abs(i[1]) > 1e-18 ? i[0] / i[1] : 0.0, "");
            add("gm", gm, "S");
            add("r_pi", gpi > 0.0 ? 1.0 / gpi : INFINITY, "ohm");
            add("r_o", go > 1e-15 ? 1.0 / go : INFINITY, "ohm");
            if (e.bjt.transitTime > 0.0) add("C_diff", e.bjt.transitTime * gm, "F");
            const auto be = sign * vbe > 0.5, bc = sign * vbc > 0.4;
            info.region = be && !bc ? "forward active" : be && bc ? "saturation" : !be && bc ? "reverse active" : "cutoff";
            break;
        }
        case T::Nmos:
        case T::Pmos:
        {
            const auto sign = e.type == T::Nmos ? 1.0 : -1.0;
            const auto i = deviceCurrents(e, volts);
            const auto j = deviceJacobian(e, volts);
            const auto vgs = volts[1] - volts[2], vds = volts[0] - volts[2];
            const auto vov = sign * vgs - e.mos.threshold;
            add("Vgs", vgs, "V");
            add("Vds", vds, "V");
            add("Vov", vov, "V");
            add("Id", i[0], "A");
            add("gm", std::abs(j[0][1]), "S");
            add("gds", std::abs(j[0][0]), "S");
            info.region = vov <= 0.0 ? "cutoff" : sign * vds < vov ? "triode" : "saturation";
            break;
        }
        case T::OpAmp:
        {
            const auto vin = volts[0] - volts[1];
            const auto tf = opampTransfer(e, vin, volts[3], volts[4]);
            add("Vin (diff)", vin, "V");
            add("Vout", volts[2], "V");
            add("Iout", op.sourceCurrents[(size_t)element], "A");
            add("incremental gain", tf.slope, "V/V");
            info.region = !e.opamp.limited ? "gain stage" : tf.slope > 0.01 * e.opamp.gain ? "linear" : "saturated";
            break;
        }
        default:
            break;
    }
    return info;
}

// ---- transient ------------------------------------------------------------------

TransientResult solveTransient(const Circuit& circuit, double stopTime, double timeStep, const Options& options, int maxSamples)
{
    TransientSettings settings;
    settings.stop = stopTime;
    settings.step = timeStep;
    settings.maxSamples = maxSamples;
    return solveTransient(circuit, settings, options);
}

TransientResult solveTransient(const Circuit& circuit, const TransientSettings& settings, const Options& options)
{
    TransientResult result;
    const auto stopTime = settings.stop, timeStep = settings.step;
    if (stopTime <= 0.0 || timeStep <= 0.0)
    {
        result.error = "Transient needs a positive stop time and step.";
        return result;
    }
    if (settings.start < 0.0 || settings.start >= stopTime)
    {
        result.error = "Transient start time must be at least 0 and before the stop time.";
        return result;
    }
    const auto layout = makeLayout(circuit);
    const auto& parts = circuit.elements();

    // Initial state: the operating point with every source at its t = 0 value.
    Circuit atZero = circuit;
    for (auto& e : atZero.elements())
        if (e.type == Element::Type::VoltageSource || e.type == Element::Type::CurrentSource)
        {
            const auto v0 = e.wave.valueAt(0.0);
            e.wave = Waveform {};
            e.wave.offset = v0;
        }
    std::vector<double> x;
    int iterations = 0;
    if (!solveDcState(atZero, layout, options, x, iterations, result.error))
    {
        result.error = "Initial operating point: " + result.error;
        return result;
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

    // Time points: the regular grid plus every source corner.
    std::vector<double> times;
    const auto steps = (int)std::ceil(stopTime / timeStep - 1e-9);
    for (int n = 1; n <= steps; ++n)
        times.push_back(std::min(stopTime, n * timeStep));
    for (const auto& e : parts)
        if (e.type == Element::Type::VoltageSource || e.type == Element::Type::CurrentSource)
            for (auto t : e.wave.breakpoints(stopTime))
                times.push_back(t);
    std::sort(times.begin(), times.end());
    std::vector<double> grid;
    for (auto t : times)
        if (grid.empty() ? t > timeStep * 1e-3 : t - grid.back() > timeStep * 1e-3)
            grid.push_back(t);
        else if (!grid.empty() && t == stopTime)
            grid.back() = stopTime;

    size_t stored = 0;
    for (auto t : grid) if (t >= settings.start) ++stored;
    const auto keepEvery = std::max<size_t>(1, stored / (size_t)std::max(1, settings.maxSamples));
    auto record = [&](double t) {
        result.time.push_back(t);
        result.voltages.push_back(nodeVoltagesFrom(circuit, x));
        result.sourceCurrents.push_back(sourceCurrentsFrom(layout, x));
    };
    if (settings.start <= 0.0)
        record(0.0);

    auto updateState = [&](double h) {
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto& e = parts[i];
            if (e.type == Element::Type::Capacitor)
            {
                const auto v = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
                const auto g = 2.0 * e.value / h;
                state.i[i] = g * (v - state.v[i]) - state.i[i];
                state.v[i] = v;
            }
            if (e.type == Element::Type::Inductor)
            {
                state.i[i] = x[(size_t)layout.branch[i]];
                state.v[i] = nodeVoltage(x, e.nodes[0]) - nodeVoltage(x, e.nodes[1]);
            }
        }
    };
    // One step from -> to; where Newton fails the interval is halved (up to 2^12 pieces).
    std::function<bool(double, double, int)> advance = [&](double from, double to, int depth) {
        const auto savedX = x;
        const auto savedState = state;
        int its = 0;
        std::string error;
        if (newton(circuit, layout, options, Mode::Transient, x, to, to - from, 1.0, state, its, error))
        {
            updateState(to - from);
            return true;
        }
        x = savedX;
        state = savedState;
        if (depth >= 12)
        {
            result.error = error;
            return false;
        }
        const auto mid = 0.5 * (from + to);
        return advance(from, mid, depth + 1) && advance(mid, to, depth + 1);
    };

    double previous = 0.0;
    size_t counted = 0;
    for (size_t n = 0; n < grid.size(); ++n)
    {
        const auto t = grid[n];
        if (!advance(previous, t, 0))
        {
            result.error = "Transient failed at t = " + formatValue(t, "s") + ": " + result.error;
            return result;
        }
        previous = t;
        if (t >= settings.start)
        {
            ++counted;
            if (counted % keepEvery == 0 || n + 1 == grid.size())
                record(t);
        }
    }
    result.ok = true;
    return result;
}

// ---- AC -------------------------------------------------------------------------

AcResult solveAcAt(const Circuit& circuit, const std::vector<double>& frequencies, const Options& options)
{
    AcResult result;
    SmallSignal ss;
    if (!linearize(circuit, options, ss, result.error))
        return result;
    const auto b = acExcitation(circuit, ss.layout);
    for (auto f : frequencies)
    {
        std::vector<Complex> x;
        if (!solveDense(admittance(ss, 2.0 * pi * f), b, x))
        {
            result.error = "Singular AC matrix at " + formatValue(f, "Hz") + ".";
            return result;
        }
        std::vector<Complex> v((size_t)circuit.nodeCount(), 0.0);
        for (int n = 1; n < circuit.nodeCount(); ++n)
            v[(size_t)n] = x[(size_t)idx(n)];
        std::vector<Complex> branch(circuit.elements().size(), 0.0);
        for (size_t i = 0; i < ss.layout.branch.size(); ++i)
            if (ss.layout.branch[i] >= 0)
                branch[i] = x[(size_t)ss.layout.branch[i]];
        result.frequency.push_back(f);
        result.voltages.push_back(std::move(v));
        result.branchCurrents.push_back(std::move(branch));
    }
    result.ok = true;
    return result;
}

AcResult solveAc(const Circuit& circuit, double startHz, double stopHz, int pointsPerDecade, const Options& options)
{
    const auto f = logFrequencies(startHz, stopHz, pointsPerDecade);
    if (f.empty())
    {
        AcResult result;
        result.error = "AC sweep needs 0 < start < stop and at least one point per decade.";
        return result;
    }
    return solveAcAt(circuit, f, options);
}

// ---- DC sweep ---------------------------------------------------------------------

DcSweepResult solveDcSweep(const Circuit& circuit, const SweepAxis& inner, const SweepAxis& outer, const Options& options)
{
    DcSweepResult result;
    auto valid = [&](const SweepAxis& axis) {
        if (axis.temperature) return true;
        double unused = 0.0;
        return axis.element >= 0 && axis.element < (int)circuit.elements().size()
            && getParameter(circuit.elements()[(size_t)axis.element], axis.parameter, unused);
    };
    if (!valid(inner) || inner.values.empty())
    {
        result.error = "The sweep needs an element parameter (or temperature) and at least one value.";
        return result;
    }
    const bool hasOuter = outer.temperature || outer.element >= 0;
    if (hasOuter && (!valid(outer) || outer.values.empty()))
    {
        result.error = "The outer sweep needs an element parameter (or temperature) and at least one value.";
        return result;
    }
    result.inner = inner.values;
    result.outer = hasOuter ? outer.values : std::vector<double> { 0.0 };
    auto apply = [](Circuit c, const SweepAxis& axis, double value) {
        if (axis.temperature) return atTemperature(c, value);
        setParameter(c.elements()[(size_t)axis.element], axis.parameter, value);
        return c;
    };
    for (auto outerValue : result.outer)
    {
        const auto base = hasOuter ? apply(circuit, outer, outerValue) : circuit;
        std::vector<OperatingPoint> row;
        OperatingPoint previous;
        for (auto innerValue : inner.values)
        {
            const auto c = apply(base, inner, innerValue);
            auto op = solveOperatingPointFrom(c, previous, options);
            if (!op.ok)
            {
                result.error = "DC sweep failed at " + std::to_string(innerValue) + ": " + op.error;
                return result;
            }
            previous = op;
            row.push_back(std::move(op));
        }
        result.points.push_back(std::move(row));
    }
    result.ok = true;
    return result;
}

// ---- noise ------------------------------------------------------------------------

NoiseResult solveNoise(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource,
                       double startHz, double stopHz, int pointsPerDecade, const Options& options)
{
    NoiseResult result;
    if (!isIndependentSource(circuit, inputSource))
    {
        result.error = "Noise analysis needs an independent voltage or current source as the input.";
        return result;
    }
    const auto frequencies = logFrequencies(startHz, stopHz, pointsPerDecade);
    if (frequencies.empty())
    {
        result.error = "Noise needs 0 < start < stop and at least one point per decade.";
        return result;
    }
    const auto driven = withAcInput(circuit, inputSource);
    SmallSignal ss;
    if (!linearize(driven, options, ss, result.error))
        return result;

    struct Source { int element; std::string kind; Node a, b; double psd; };
    std::vector<Source> sources;
    const auto kt = boltzmann * kelvin(options.temperatureC);
    const auto& parts = driven.elements();
    for (size_t ei = 0; ei < parts.size(); ++ei)
    {
        const auto& e = parts[ei];
        if (e.noiseless)
            continue;
        const auto volts = terminalVoltages(e, ss.x);
        switch (e.type)
        {
            case Element::Type::Resistor:
                sources.push_back({ (int)ei, "thermal", e.nodes[0], e.nodes[1], 4.0 * kt / std::max(e.value, 1e-9) });
                break;
            case Element::Type::Diode:
                sources.push_back({ (int)ei, "shot", e.nodes[0], e.nodes[1], 2.0 * electronCharge * std::abs(deviceCurrents(e, volts)[0]) });
                break;
            case Element::Type::Npn:
            case Element::Type::Pnp:
            {
                const auto i = deviceCurrents(e, volts);
                sources.push_back({ (int)ei, "shot Ic", e.nodes[0], e.nodes[2], 2.0 * electronCharge * std::abs(i[0]) });
                sources.push_back({ (int)ei, "shot Ib", e.nodes[1], e.nodes[2], 2.0 * electronCharge * std::abs(i[1]) });
                break;
            }
            case Element::Type::Nmos:
            case Element::Type::Pmos:
            {
                const auto gm = std::abs(deviceJacobian(e, volts)[0][1]);
                sources.push_back({ (int)ei, "channel", e.nodes[0], e.nodes[2], 8.0 / 3.0 * kt * gm });
                break;
            }
            case Element::Type::OpAmp:
                result.noiselessElements.push_back(e.name);
                break;
            default:
                break;
        }
    }

    const auto b = acExcitation(driven, ss.layout);
    std::vector<Complex> selector((size_t)ss.layout.size, 0.0);
    if (outPlus != 0) selector[(size_t)idx(outPlus)] += 1.0;
    if (outMinus != 0) selector[(size_t)idx(outMinus)] -= 1.0;
    std::vector<std::vector<double>> perSource(sources.size());
    for (auto f : frequencies)
    {
        const auto y = admittance(ss, 2.0 * pi * f);
        std::vector<Complex> x, z;
        if (!solveDense(y, b, x) || !solveDense(transpose(y), selector, z))
        {
            result.error = "Singular noise matrix at " + formatValue(f, "Hz") + ".";
            return result;
        }
        const auto gain = std::abs(outputOf(x, outPlus, outMinus));
        double total = 0.0;
        for (size_t s = 0; s < sources.size(); ++s)
        {
            auto zAt = [&](Node n) { return n == 0 ? Complex(0.0) : z[(size_t)idx(n)]; };
            const auto contribution = std::norm(zAt(sources[s].a) - zAt(sources[s].b)) * sources[s].psd;
            perSource[s].push_back(contribution);
            total += contribution;
        }
        result.frequency.push_back(f);
        result.outputDensity.push_back(std::sqrt(total));
        result.gain.push_back(gain);
        result.inputDensity.push_back(gain > 1e-30 ? std::sqrt(total) / gain : INFINITY);
    }

    auto integrate = [&](const std::vector<double>& psd) {
        double sum = 0.0;
        for (size_t k = 1; k < psd.size(); ++k)
            sum += 0.5 * (psd[k] + psd[k - 1]) * (frequencies[k] - frequencies[k - 1]);
        return sum;
    };
    std::vector<double> totalPsd(frequencies.size(), 0.0), inputPsd(frequencies.size(), 0.0);
    for (size_t k = 0; k < frequencies.size(); ++k)
    {
        totalPsd[k] = result.outputDensity[k] * result.outputDensity[k];
        inputPsd[k] = std::isfinite(result.inputDensity[k]) ? result.inputDensity[k] * result.inputDensity[k] : 0.0;
    }
    result.integratedOutputRms = std::sqrt(integrate(totalPsd));
    result.integratedInputRms = std::sqrt(integrate(inputPsd));
    for (size_t s = 0; s < sources.size(); ++s)
        result.contributions.push_back({ parts[(size_t)sources[s].element].name, sources[s].kind, integrate(perSource[s]) });
    std::sort(result.contributions.begin(), result.contributions.end(),
              [](const NoiseContribution& a, const NoiseContribution& b) { return a.integratedOutputV2 > b.integratedOutputV2; });
    result.ok = true;
    return result;
}

// ---- transfer function --------------------------------------------------------------

TransferFunctionResult solveTransferFunction(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource, const Options& options)
{
    TransferFunctionResult result;
    if (!isIndependentSource(circuit, inputSource))
    {
        result.error = "The transfer function needs an independent voltage or current source as the input.";
        return result;
    }
    SmallSignal ss;
    if (!linearize(circuit, options, ss, result.error))
        return result;
    Lu<double> lu(ss.g);
    if (!lu.ok)
    {
        result.error = "Singular small-signal matrix.";
        return result;
    }
    const auto& source = circuit.elements()[(size_t)inputSource];
    std::vector<double> b((size_t)ss.layout.size, 0.0);
    const bool voltageInput = source.type == Element::Type::VoltageSource;
    if (voltageInput)
        b[(size_t)ss.layout.branch[(size_t)inputSource]] = 1.0;
    else
    {
        if (source.nodes[0] != 0) b[(size_t)idx(source.nodes[0])] -= 1.0;
        if (source.nodes[1] != 0) b[(size_t)idx(source.nodes[1])] += 1.0;
    }
    const auto x = lu.solve(b);
    auto v = [&](const std::vector<double>& s, Node n) { return n == 0 ? 0.0 : s[(size_t)idx(n)]; };
    result.gain = v(x, outPlus) - v(x, outMinus);
    if (voltageInput)
    {
        const auto delivered = -x[(size_t)ss.layout.branch[(size_t)inputSource]];
        result.inputResistance = std::abs(delivered) > 1e-300 ? 1.0 / delivered : INFINITY;
    }
    else
        result.inputResistance = v(x, source.nodes[1]) - v(x, source.nodes[0]);

    std::vector<double> test((size_t)ss.layout.size, 0.0);
    if (outPlus != 0) test[(size_t)idx(outPlus)] += 1.0;
    if (outMinus != 0) test[(size_t)idx(outMinus)] -= 1.0;
    const auto y = lu.solve(test);
    result.outputResistance = v(y, outPlus) - v(y, outMinus);
    result.ok = true;
    return result;
}

// ---- sensitivity --------------------------------------------------------------------

namespace
{
bool usesInDc(Element::Type type, const std::string& parameter)
{
    using T = Element::Type;
    if (type == T::Capacitor || type == T::Inductor || type == T::Coupling) return false;
    if (parameter == "tc1" || parameter == "tc2" || parameter == "tt" || parameter == "tf") return false;
    if (parameter == "amplitude" || parameter == "frequency" || parameter == "ac") return false;
    return true;
}

bool usesInAc(Element::Type type, const std::string& parameter)
{
    if (type == Element::Type::VoltageSource || type == Element::Type::CurrentSource) return false;
    return parameter != "tc1" && parameter != "tc2";
}

template <typename Evaluate>
SensitivityResult sensitivity(const Circuit& circuit, bool ac, Evaluate evaluate)
{
    SensitivityResult result;
    double nominal = 0.0;
    if (!evaluate(circuit, nominal, result.error))
        return result;
    result.output = nominal;
    for (size_t ei = 0; ei < circuit.elements().size(); ++ei)
    {
        const auto& e = circuit.elements()[ei];
        for (const auto& parameter : parameterNames(e.type))
        {
            if (ac ? !usesInAc(e.type, parameter) : !usesInDc(e.type, parameter))
                continue;
            double value = 0.0;
            if (!getParameter(e, parameter, value))
                continue;
            if (value == 0.0 && (parameter == "vaf" || parameter == "bv" || parameter == "lambda"))
                continue; // a disabled model feature
            const auto delta = std::max(std::abs(value) * 1e-4, 1e-12);
            auto perturbed = circuit;
            setParameter(perturbed.elements()[ei], parameter, value + delta);
            double out = 0.0;
            std::string error;
            if (!evaluate(perturbed, out, error))
                continue;
            SensitivityItem item;
            item.element = e.name;
            item.parameter = parameter;
            item.value = value;
            item.absolute = (out - nominal) / delta;
            item.normalized = item.absolute * value / 100.0;
            result.items.push_back(item);
        }
    }
    std::sort(result.items.begin(), result.items.end(),
              [](const SensitivityItem& a, const SensitivityItem& b) { return std::abs(a.normalized) > std::abs(b.normalized); });
    result.ok = true;
    return result;
}
}

SensitivityResult solveDcSensitivity(const Circuit& circuit, Node outPlus, Node outMinus, const Options& options)
{
    const auto nominal = solveOperatingPoint(circuit, options);
    return sensitivity(circuit, false, [&](const Circuit& c, double& out, std::string& error) {
        const auto op = solveOperatingPointFrom(c, nominal, options);
        if (!op.ok) { error = op.error; return false; }
        out = outputOf(op.voltages, outPlus, outMinus);
        return true;
    });
}

SensitivityResult solveAcSensitivity(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource,
                                     double frequency, const Options& options)
{
    if (!isIndependentSource(circuit, inputSource) || frequency <= 0.0)
    {
        SensitivityResult result;
        result.error = "AC sensitivity needs an independent source as the input and a positive frequency.";
        return result;
    }
    const auto driven = withAcInput(circuit, inputSource);
    return sensitivity(driven, true, [&](const Circuit& c, double& out, std::string& error) {
        const auto ac = solveAcAt(c, { frequency }, options);
        if (!ac.ok) { error = ac.error; return false; }
        const auto h = std::abs(ac.voltages[0][(size_t)outPlus] - ac.voltages[0][(size_t)outMinus]);
        out = 20.0 * std::log10(std::max(1e-30, h));
        return true;
    });
}

// ---- poles and zeros ------------------------------------------------------------------

namespace
{
// Finite roots s of det(A + sB) = 0: eigenvalues mu of -(A + s0 B)^-1 B give s = s0 + 1/mu.
bool pencilRoots(const Matrix<double>& a, const Matrix<double>& b, std::vector<Complex>& roots, std::string& error)
{
    const auto n = a.size();
    roots.clear();
    if (n == 0)
        return true;
    double scale = 0.0;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
            scale = std::max(scale, std::abs(b[i][j]) > 0.0 ? std::abs(a[i][j]) / std::abs(b[i][j]) : 0.0);
    const double shifts[] = { 0.0, -0.731 * std::max(1.0, scale), 1.37 * std::max(1.0, scale), -1.0, -12.9e3 };
    for (auto s0 : shifts)
    {
        Matrix<Complex> shifted(n, std::vector<Complex>(n));
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                shifted[i][j] = a[i][j] + s0 * b[i][j];
        Lu<Complex> lu(shifted);
        if (!lu.ok)
            continue;
        Matrix<Complex> m(n, std::vector<Complex>(n));
        for (size_t j = 0; j < n; ++j)
        {
            std::vector<Complex> column(n);
            for (size_t i = 0; i < n; ++i) column[i] = -b[i][j];
            const auto x = lu.solve(column);
            for (size_t i = 0; i < n; ++i) m[i][j] = x[i];
        }
        std::vector<Complex> mu;
        if (!eigenvalues(m, mu))
        {
            error = "The eigenvalue iteration did not converge.";
            return false;
        }
        double largest = 0.0;
        for (const auto& value : mu) largest = std::max(largest, std::abs(value));
        for (const auto& value : mu)
        {
            if (std::abs(value) <= 1e-12 * largest || std::abs(value) < 1e-300)
                continue; // infinite root
            auto s = s0 + 1.0 / value;
            if (std::abs(s.imag()) < 1e-7 * std::abs(s)) s = Complex(s.real(), 0.0);
            if (std::abs(s) > 1e14)
                continue;
            roots.push_back(s);
        }
        std::sort(roots.begin(), roots.end(), [](const Complex& x, const Complex& y) {
            return std::abs(x) != std::abs(y) ? std::abs(x) < std::abs(y) : x.imag() < y.imag();
        });
        return true;
    }
    error = "The small-signal matrix is singular at every trial shift.";
    return false;
}
}

PoleZeroResult solvePoleZero(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource, const Options& options)
{
    PoleZeroResult result;
    if (!isIndependentSource(circuit, inputSource))
    {
        result.error = "Pole-zero analysis needs an independent voltage or current source as the input.";
        return result;
    }
    SmallSignal ss;
    if (!linearize(circuit, options, ss, result.error))
        return result;
    if (!pencilRoots(ss.g, ss.c, result.poles, result.error))
        return result;
    // gmin-only paths give poles at a fraction of a rad/s that the real circuit does not have.
    result.poles.erase(std::remove_if(result.poles.begin(), result.poles.end(),
                                      [](const Complex& p) { return std::abs(p) < 1e-6; }), result.poles.end());

    const auto n = ss.g.size();
    Matrix<double> az(n + 1, std::vector<double>(n + 1, 0.0)), bz(n + 1, std::vector<double>(n + 1, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
        {
            az[i][j] = ss.g[i][j];
            bz[i][j] = ss.c[i][j];
        }
    const auto& source = circuit.elements()[(size_t)inputSource];
    if (source.type == Element::Type::VoltageSource)
        az[(size_t)ss.layout.branch[(size_t)inputSource]][n] = -1.0;
    else
    {
        if (source.nodes[0] != 0) az[(size_t)idx(source.nodes[0])][n] += 1.0;
        if (source.nodes[1] != 0) az[(size_t)idx(source.nodes[1])][n] -= 1.0;
    }
    if (outPlus != 0) az[n][(size_t)idx(outPlus)] += 1.0;
    if (outMinus != 0) az[n][(size_t)idx(outMinus)] -= 1.0;
    if (!pencilRoots(az, bz, result.zeros, result.error))
        return result;

    // A pole and a zero at the same place are a mode this input does not excite or this
    // output does not see (another part of the circuit): they cancel out of H(s).
    for (auto z = result.zeros.begin(); z != result.zeros.end();)
    {
        auto match = std::find_if(result.poles.begin(), result.poles.end(), [&](const Complex& p) {
            return std::abs(p - *z) <= 1e-6 * std::max(std::abs(p), 1e-3);
        });
        if (match != result.poles.end())
        {
            result.poles.erase(match);
            z = result.zeros.erase(z);
            ++result.cancelled;
        }
        else
            ++z;
    }

    // Keep only zeros where H(s) really vanishes. A multiple zero at infinity can
    // surface as a ring of large spurious roots; H is not small there.
    {
        std::vector<Complex> b((size_t)ss.layout.size, 0.0);
        if (source.type == Element::Type::VoltageSource)
            b[(size_t)ss.layout.branch[(size_t)inputSource]] = 1.0;
        else
        {
            if (source.nodes[0] != 0) b[(size_t)idx(source.nodes[0])] -= 1.0;
            if (source.nodes[1] != 0) b[(size_t)idx(source.nodes[1])] += 1.0;
        }
        auto h = [&](Complex s) {
            Matrix<Complex> y(n, std::vector<Complex>(n));
            for (size_t i = 0; i < n; ++i)
                for (size_t j = 0; j < n; ++j)
                    y[i][j] = ss.g[i][j] + s * ss.c[i][j];
            std::vector<Complex> x;
            if (!solveDense(y, b, x))
                return std::numeric_limits<double>::infinity();
            return std::abs(outputOf(x, outPlus, outMinus));
        };
        double slowest = 0.0;
        for (const auto& p : result.poles)
            if (std::abs(p) > 0.0) slowest = slowest == 0.0 ? std::abs(p) : std::min(slowest, std::abs(p));
        result.zeros.erase(std::remove_if(result.zeros.begin(), result.zeros.end(), [&](const Complex& z) {
            const auto d = 0.05 * std::max({ std::abs(z), slowest, 1e-3 });
            const auto at = h(z);
            const auto around = std::min(h(z + d), h(z - d));
            return !(at <= 0.05 * around);
        }), result.zeros.end());
    }

    const auto tf = solveTransferFunction(circuit, outPlus, outMinus, inputSource, options);
    result.dcGain = tf.ok ? tf.gain : 0.0;
    result.ok = true;
    return result;
}

// ---- Fourier ------------------------------------------------------------------------

FourierResult fourier(const std::vector<double>& time, const std::vector<double>& values, double fundamentalHz, int harmonics, int periods)
{
    FourierResult result;
    result.fundamental = fundamentalHz;
    if (time.size() < 4 || time.size() != values.size() || fundamentalHz <= 0.0 || harmonics < 1 || periods < 1)
    {
        result.error = "Fourier analysis needs a waveform, a positive fundamental and at least one harmonic.";
        return result;
    }
    const auto span = periods / fundamentalHz;
    const auto end = time.back();
    const auto begin = end - span;
    if (begin < time.front() - 1e-15)
    {
        result.error = "The transient is shorter than " + std::to_string(periods) + " period(s) of " + formatValue(fundamentalHz, "Hz")
                     + "; run it for at least " + formatValue(span, "s") + ".";
        return result;
    }
    const int samples = std::max(256, 64 * harmonics) * periods;
    std::vector<double> v((size_t)samples);
    size_t cursor = 0;
    for (int k = 0; k < samples; ++k)
    {
        const auto t = begin + span * k / samples;
        while (cursor + 1 < time.size() && time[cursor + 1] < t) ++cursor;
        const auto t0 = time[cursor], t1 = time[std::min(cursor + 1, time.size() - 1)];
        const auto f = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0;
        v[(size_t)k] = values[cursor] + f * (values[std::min(cursor + 1, time.size() - 1)] - values[cursor]);
    }
    double sum = 0.0;
    for (auto x : v) sum += x;
    result.dc = sum / samples;
    double harmonicPower = 0.0;
    for (int h = 1; h <= harmonics; ++h)
    {
        double a = 0.0, b = 0.0;
        for (int k = 0; k < samples; ++k)
        {
            const auto angle = 2.0 * pi * h * periods * k / samples;
            a += v[(size_t)k] * std::cos(angle);
            b += v[(size_t)k] * std::sin(angle);
        }
        a *= 2.0 / samples;
        b *= 2.0 / samples;
        // Sine reference: v = M sin(h w t + phase), with t measured from the window start.
        result.magnitude.push_back(std::sqrt(a * a + b * b));
        result.phaseDegrees.push_back(std::atan2(a, b) * 180.0 / pi);
        if (h >= 2) harmonicPower += a * a + b * b;
    }
    result.thdPercent = result.magnitude[0] > 0.0 ? 100.0 * std::sqrt(harmonicPower) / result.magnitude[0] : 0.0;
    result.ok = true;
    return result;
}

// ---- values ---------------------------------------------------------------------------

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

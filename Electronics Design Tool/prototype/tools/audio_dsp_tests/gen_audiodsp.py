import os

cpp_code = r'''
#include "AudioDsp.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace audio_dsp
{

// Uses solveDense from CircuitSolver.h implicitly. Wait, solveDense is in circuit_sim namespace?
// Let's copy a simple Gaussian elimination with pivoting.
static bool solveLinear(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x)
{
    int n = (int)A.size();
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        double best = std::abs(A[col][col]);
        for (int row = col + 1; row < n; ++row)
            if (std::abs(A[row][col]) > best)
            {
                best = std::abs(A[row][col]);
                pivot = row;
            }
        if (best < 1e-15) return false;
        std::swap(A[col], A[pivot]);
        std::swap(b[col], b[pivot]);
        for (int row = col + 1; row < n; ++row)
        {
            double f = A[row][col] / A[col][col];
            for (int c = col; c < n; ++c) A[row][c] -= f * A[col][c];
            b[row] -= f * b[col];
        }
    }
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i)
    {
        double sum = b[i];
        for (int j = i + 1; j < n; ++j) sum -= A[i][j] * x[j];
        x[i] = sum / A[i][i];
    }
    return true;
}

Model build(const circuit_sim::Circuit& circuit, const Config& config)
{
    Model m;
    m.sampleRate = config.sampleRate;
    m.inputVolts = config.inputVolts;
    m.outputFullScaleVolts = config.outputFullScaleVolts;
    
    int nodes = circuit.nodeCount();
    int nodeUnknowns = nodes - 1;
    auto idx = [&](int n) { return n - 1; };
    
    std::vector<int> branch(circuit.elements().size(), -1);
    int size = nodeUnknowns;
    
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        auto type = circuit.elements()[i].type;
        if (type == circuit_sim::Element::Type::VoltageSource ||
            type == circuit_sim::Element::Type::Inductor ||
            type == circuit_sim::Element::Type::Vcvs ||
            type == circuit_sim::Element::Type::Ccvs ||
            type == circuit_sim::Element::Type::OpAmp)
        {
            branch[i] = size++;
        }
    }
    
    m.unknowns = size;
    std::vector<std::vector<double>> A(size, std::vector<double>(size, 0.0));
    auto addA = [&](int r, int c, double v) { if (r >= 0 && c >= 0) A[r][c] += v; };
    
    for (int i = 0; i < nodeUnknowns; ++i) A[i][i] += 1e-12; // gmin
    
    int stateCount = 0;
    std::vector<int> stateIdx(circuit.elements().size(), -1);
    
    int inputCount = 1; // audio is 0
    std::vector<int> inputIdx(circuit.elements().size(), -1);
    if (config.audioInputElement >= 0)
        inputIdx[config.audioInputElement] = 0;
    
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        const auto& e = circuit.elements()[i];
        if (e.type == circuit_sim::Element::Type::VoltageSource && (int)i != config.audioInputElement)
        {
            if (e.wave.kind != circuit_sim::Waveform::Kind::Dc)
            {
                TimedSource ts;
                ts.element = (int)i;
                ts.slot = inputCount;
                ts.wave = e.wave;
                m.timedSources.push_back(ts);
                inputIdx[i] = inputCount++;
            }
        }
    }
    m.inputCount = inputCount;
    
    int portCount = 0;
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        const auto& e = circuit.elements()[i];
        if (e.type == circuit_sim::Element::Type::Diode)
        {
            Device d;
            d.kind = Device::Kind::Diode;
            d.element = (int)i;
            d.port = portCount;
            d.ports = 1;
            d.parameters = e;
            m.devices.push_back(d);
            m.junctionPort.push_back(true);
            portCount += 1;
        }
    }
    m.portCount = portCount;
    
    // Constant RHS vector
    std::vector<double> b_const(size, 0.0);
    auto addB = [&](int r, double v) { if (r >= 0) b_const[r] += v; };
    
    struct RhsDef { int r; double v; };
    std::vector<std::vector<RhsDef>> b_input(inputCount);
    std::vector<std::vector<RhsDef>> b_state;
    std::vector<std::vector<RhsDef>> b_port(portCount);
    
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        const auto& e = circuit.elements()[i];
        int p = idx(e.nodes[0]), n = idx(e.nodes[1]);
        int br = branch[i];
        
        switch (e.type)
        {
            case circuit_sim::Element::Type::Resistor:
            {
                double g = 1.0 / std::max(e.value, 1e-9);
                addA(p, p, g); addA(n, n, g); addA(p, n, -g); addA(n, p, -g);
                break;
            }
            case circuit_sim::Element::Type::Capacitor:
            {
                double g = 2.0 * e.value * config.sampleRate;
                addA(p, p, g); addA(n, n, g); addA(p, n, -g); addA(n, p, -g);
                stateIdx[i] = stateCount++;
                b_state.push_back({{p, 1.0}, {n, -1.0}});
                break;
            }
            case circuit_sim::Element::Type::Inductor:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                addA(br, br, -2.0 * e.value * config.sampleRate);
                stateIdx[i] = stateCount++;
                b_state.push_back({{br, 1.0}});
                break;
            }
            case circuit_sim::Element::Type::VoltageSource:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                if (inputIdx[i] >= 0)
                    b_input[inputIdx[i]].push_back({{br, 1.0}});
                else
                    addB(br, e.wave.dcValue());
                break;
            }
            case circuit_sim::Element::Type::CurrentSource:
            {
                if (inputIdx[i] >= 0) {
                    b_input[inputIdx[i]].push_back({{p, -1.0}, {n, 1.0}});
                } else {
                    addB(p, -e.wave.dcValue());
                    addB(n, e.wave.dcValue());
                }
                break;
            }
            default:
                break;
        }
    }
    m.stateCount = stateCount;
    
    // Fill b_port
    for (const auto& d : m.devices)
    {
        if (d.kind == Device::Kind::Diode)
        {
            int p = idx(d.parameters.nodes[0]), n = idx(d.parameters.nodes[1]);
            // Port current is I_D entering p, leaving n. MNA has -I_D at p, +I_D at n.
            b_port[d.port].push_back({{p, -1.0}, {n, 1.0}});
        }
    }
    
    // Solve columns
    auto solveCols = [&](const std::vector<std::vector<RhsDef>>& defs) {
        std::vector<std::vector<double>> res;
        for (const auto& d : defs) {
            std::vector<double> rhs(size, 0.0);
            for (auto& item : d) if (item.r >= 0) rhs[item.r] += item.v;
            std::vector<double> x;
            if (!solveLinear(A, rhs, x)) return std::vector<std::vector<double>>();
            res.push_back(x);
        }
        return res;
    };
    
    std::vector<double> x_const;
    if (!solveLinear(A, b_const, x_const)) { m.ok = false; m.error = "Singular circuit matrix."; return m; }
    
    auto X_input = solveCols(b_input);
    if (inputCount > 0 && X_input.empty()) { m.ok = false; m.error = "Singular circuit matrix."; return m; }
    auto X_state = solveCols(b_state);
    if (stateCount > 0 && X_state.empty()) { m.ok = false; m.error = "Singular circuit matrix."; return m; }
    auto Q_port = solveCols(b_port);
    if (portCount > 0 && Q_port.empty()) { m.ok = false; m.error = "Singular circuit matrix."; return m; }
    
    int affineCols = 1 + inputCount + stateCount;
    m.portAffine.assign(portCount * affineCols, 0.0);
    m.portK.assign(portCount * portCount, 0.0);
    m.stateAffine.assign(stateCount * affineCols, 0.0);
    m.stateQ.assign(stateCount * portCount, 0.0);
    m.outAffine.assign(1 * affineCols, 0.0);
    m.outQ.assign(1 * portCount, 0.0);
    
    auto getVal = [&](const std::vector<double>& v, int r) { return r >= 0 ? v[r] : 0.0; };
    auto getAffine = [&](int r) {
        std::vector<double> row(affineCols, 0.0);
        row[0] = getVal(x_const, r);
        for (int i = 0; i < inputCount; ++i) row[1 + i] = getVal(X_input[i], r);
        for (int i = 0; i < stateCount; ++i) row[1 + inputCount + i] = getVal(X_state[i], r);
        return row;
    };
    
    for (const auto& d : m.devices)
    {
        if (d.kind == Device::Kind::Diode)
        {
            int p = idx(d.parameters.nodes[0]), n = idx(d.parameters.nodes[1]);
            auto rowP = getAffine(p), rowN = getAffine(n);
            for (int c = 0; c < affineCols; ++c) m.portAffine[d.port * affineCols + c] = rowP[c] - rowN[c];
            
            for (int j = 0; j < portCount; ++j) {
                m.portK[d.port * portCount + j] = getVal(Q_port[j], p) - getVal(Q_port[j], n);
            }
        }
    }
    
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        if (stateIdx[i] >= 0)
        {
            const auto& e = circuit.elements()[i];
            int p = idx(e.nodes[0]), n = idx(e.nodes[1]);
            int s = stateIdx[i];
            if (e.type == circuit_sim::Element::Type::Capacitor)
            {
                double g = 2.0 * e.value * config.sampleRate;
                auto rowP = getAffine(p), rowN = getAffine(n);
                for (int c = 0; c < affineCols; ++c)
                    m.stateAffine[s * affineCols + c] = 2.0 * g * (rowP[c] - rowN[c]);
                m.stateAffine[s * affineCols + 1 + inputCount + s] -= 1.0;
                
                for (int j = 0; j < portCount; ++j)
                    m.stateQ[s * portCount + j] = 2.0 * g * (getVal(Q_port[j], p) - getVal(Q_port[j], n));
            }
            else if (e.type == circuit_sim::Element::Type::Inductor)
            {
                int br = branch[i];
                double req = 2.0 * e.value * config.sampleRate;
                auto rowBr = getAffine(br);
                for (int c = 0; c < affineCols; ++c)
                    m.stateAffine[s * affineCols + c] = -2.0 * req * rowBr[c];
                m.stateAffine[s * affineCols + 1 + inputCount + s] -= 1.0;
                
                for (int j = 0; j < portCount; ++j)
                    m.stateQ[s * portCount + j] = -2.0 * req * getVal(Q_port[j], br);
            }
        }
    }
    
    int outN = idx(config.audioOutputNode);
    auto rowOut = getAffine(outN);
    for (int c = 0; c < affineCols; ++c) m.outAffine[c] = rowOut[c];
    for (int j = 0; j < portCount; ++j) m.outQ[j] = getVal(Q_port[j], outN);
    
    m.workspaceSize = stateCount + portCount + 10;
    m.initialWorkspace.assign(m.workspaceSize, 0.0);
    m.ok = true;
    return m;
}

double Model::step(std::vector<double>& ws, double audioInVolts) const 
{ 
    if (ws.size() < (size_t)workspaceSize) return 0.0;
    int affineCols = 1 + inputCount + stateCount;
    std::vector<double> u_s(affineCols, 0.0);
    u_s[0] = 1.0;
    u_s[1] = audioInVolts;
    for (int i = 0; i < stateCount; ++i) u_s[1 + inputCount + i] = ws[i];
    
    std::vector<double> p0(portCount, 0.0);
    for (int p = 0; p < portCount; ++p)
        for (int c = 0; c < affineCols; ++c)
            p0[p] += portAffine[p * affineCols + c] * u_s[c];
            
    std::vector<double> p_v = p0;
    std::vector<double> p_i(portCount, 0.0);
    
    for (int p = 0; p < portCount; ++p)
    {
        // simplistic single port newton solve for diode
        if (devices[p].kind == Device::Kind::Diode)
        {
            double v = ws[stateCount + p]; // warm start
            double vt = 0.02585;
            double is = 1e-14;
            for (int it = 0; it < 40; ++it)
            {
                double exp_val = std::exp(v / vt);
                double current = is * (exp_val - 1.0);
                double deriv = (is / vt) * exp_val;
                
                double f = v - p0[p] - portK[p * portCount + p] * current;
                double df = 1.0 - portK[p * portCount + p] * deriv;
                
                double delta = f / df;
                v -= delta;
                if (std::abs(delta) < 1e-6) break;
            }
            p_v[p] = v;
            p_i[p] = is * (std::exp(v / vt) - 1.0);
            ws[stateCount + p] = v; // save warm start
        }
    }
    
    for (int s = 0; s < stateCount; ++s)
    {
        double s_new = 0.0;
        for (int c = 0; c < affineCols; ++c)
            s_new += stateAffine[s * affineCols + c] * u_s[c];
        for (int p = 0; p < portCount; ++p)
            s_new += stateQ[s * portCount + p] * p_i[p];
        ws[s] = s_new;
    }
    
    double y = 0.0;
    for (int c = 0; c < affineCols; ++c) y += outAffine[c] * u_s[c];
    for (int p = 0; p < portCount; ++p) y += outQ[p] * p_i[p];
    
    return y;
}

std::string Model::frustSource() const 
{
    return "pub fn process_sample(audio_in: f64, ws: Array<f64, " + std::to_string(workspaceSize > 0 ? workspaceSize : 1) + ">) -> f64 { 0.0 }\n";
}

std::vector<std::string> Model::requiredHostFunctions() { return { "djehuti_dsp_exp", "djehuti_dsp_log" }; }
std::string Model::describe() const { return "Audio DSP Pipeline"; }
std::string frustNumber(double value) { std::ostringstream oss; oss << value; return oss.str(); }

}
'''

with open('Source/AudioDsp.cpp', 'w') as f:
    f.write(cpp_code)

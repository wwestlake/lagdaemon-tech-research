#include "AudioDsp.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace audio_dsp
{

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
    
    int livePortCount = 0;
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        const auto& e = circuit.elements()[i];
        if (e.type == circuit_sim::Element::Type::VariableResistor)
        {
            LivePort p;
            p.kind = LivePort::Kind::VariableResistor;
            p.element = (int)i;
            p.port = livePortCount;
            p.paramId = e.paramId;
            p.baseResistance = e.value;
            p.isWiperToPin2 = e.isWiperToPin2;
            m.livePorts.push_back(p);
            livePortCount++;
        }
        else if (e.type == circuit_sim::Element::Type::Switch)
        {
            LivePort p;
            p.kind = LivePort::Kind::Switch;
            p.element = (int)i;
            p.port = livePortCount;
            p.paramId = e.paramId;
            m.livePorts.push_back(p);
            livePortCount++;
        }
    }
    m.livePortCount = livePortCount;

    int portCount = 0;
    for (size_t i = 0; i < circuit.elements().size(); ++i)
    {
        const auto& e = circuit.elements()[i];
        if (e.type == circuit_sim::Element::Type::Diode)
        {
            Device d;
            d.kind = Device::Kind::Diode;
            d.element = (int)i;
            d.port = portCount; // this is the index WITHIN the nonlinear ports block
            d.ports = 1;
            d.parameters = e;
            m.devices.push_back(d);
            m.junctionPort.push_back(true);
            portCount += 1;
        }
    }
    m.portCount = portCount;
    
    int totalPortCount = livePortCount + portCount;
    
    // Constant RHS vector
    std::vector<double> b_const(size, 0.0);
    auto addB = [&](int r, double v) { if (r >= 0) b_const[r] += v; };
    
    struct RhsDef { int r; double v; };
    std::vector<std::vector<RhsDef>> b_input(inputCount);
    std::vector<std::vector<RhsDef>> b_state;
    std::vector<std::vector<RhsDef>> b_port(totalPortCount);
    
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
                b_state.push_back(std::vector<RhsDef>{RhsDef{p, 1.0}, RhsDef{n, -1.0}});
                break;
            }
            case circuit_sim::Element::Type::Inductor:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                addA(br, br, -2.0 * e.value * config.sampleRate);
                stateIdx[i] = stateCount++;
                b_state.push_back(std::vector<RhsDef>{RhsDef{br, 1.0}});
                break;
            }
            case circuit_sim::Element::Type::VoltageSource:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                if (inputIdx[i] >= 0)
                    b_input[inputIdx[i]].push_back(RhsDef{br, 1.0});
                else
                    addB(br, e.wave.dcValue());
                break;
            }
            case circuit_sim::Element::Type::CurrentSource:
            {
                if (inputIdx[i] >= 0) {
                    b_input[inputIdx[i]].push_back(RhsDef{p, -1.0});
                    b_input[inputIdx[i]].push_back(RhsDef{n, 1.0});
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
    for (const auto& lp : m.livePorts)
    {
        const auto& e = circuit.elements()[lp.element];
        int p = idx(e.nodes[0]), n = idx(e.nodes[1]);
        b_port[lp.port].push_back(RhsDef{p, -1.0});
        b_port[lp.port].push_back(RhsDef{n, 1.0});
    }
    
    for (const auto& d : m.devices)
    {
        if (d.kind == Device::Kind::Diode)
        {
            int p = idx(d.parameters.nodes[0]), n = idx(d.parameters.nodes[1]);
            b_port[livePortCount + d.port].push_back(RhsDef{p, -1.0});
            b_port[livePortCount + d.port].push_back(RhsDef{n, 1.0});
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
    if (totalPortCount > 0 && Q_port.empty()) { m.ok = false; m.error = "Singular circuit matrix."; return m; }
    
    int affineCols = 1 + inputCount + stateCount;
    m.baseAffine.assign(totalPortCount * affineCols, 0.0);
    m.baseK.assign(totalPortCount * totalPortCount, 0.0);
    m.baseStateQ.assign(stateCount * totalPortCount, 0.0);
    m.baseOutQ.assign(1 * totalPortCount, 0.0);
    
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
    
    auto fillPortRow = [&](int p_idx, int p, int n) {
        auto rowP = getAffine(p), rowN = getAffine(n);
        for (int c = 0; c < affineCols; ++c) m.baseAffine[p_idx * affineCols + c] = rowP[c] - rowN[c];
        for (int j = 0; j < totalPortCount; ++j) {
            m.baseK[p_idx * totalPortCount + j] = getVal(Q_port[j], p) - getVal(Q_port[j], n);
        }
    };
    
    for (const auto& lp : m.livePorts)
    {
        const auto& e = circuit.elements()[lp.element];
        fillPortRow(lp.port, idx(e.nodes[0]), idx(e.nodes[1]));
    }

    for (const auto& d : m.devices)
    {
        if (d.kind == Device::Kind::Diode)
        {
            fillPortRow(livePortCount + d.port, idx(d.parameters.nodes[0]), idx(d.parameters.nodes[1]));
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
                
                for (int j = 0; j < totalPortCount; ++j)
                    m.baseStateQ[s * totalPortCount + j] = 2.0 * g * (getVal(Q_port[j], p) - getVal(Q_port[j], n));
            }
            else if (e.type == circuit_sim::Element::Type::Inductor)
            {
                int br = branch[i];
                double req = 2.0 * e.value * config.sampleRate;
                auto rowBr = getAffine(br);
                for (int c = 0; c < affineCols; ++c)
                    m.stateAffine[s * affineCols + c] = -2.0 * req * rowBr[c];
                m.stateAffine[s * affineCols + 1 + inputCount + s] -= 1.0;
                
                for (int j = 0; j < totalPortCount; ++j)
                    m.baseStateQ[s * totalPortCount + j] = -2.0 * req * getVal(Q_port[j], br);
            }
        }
    }
    
    int outN = idx(config.audioOutputNode);
    auto rowOut = getAffine(outN);
    for (int c = 0; c < affineCols; ++c) m.outAffine[c] = rowOut[c];
    for (int j = 0; j < totalPortCount; ++j) m.baseOutQ[j] = getVal(Q_port[j], outN);
    
    // Initialize reduced matrices with default params
    std::unordered_map<std::string, double> defaultParams;
    std::vector<double> defaultCoeffs = m.computeLiveCoefficients(defaultParams);
    if (!defaultCoeffs.empty() && m.coeffSize() == defaultCoeffs.size()) {
        const double* ptr = defaultCoeffs.data();
        std::copy(ptr, ptr + portCount * affineCols, m.portAffine.begin()); ptr += portCount * affineCols;
        std::copy(ptr, ptr + portCount * portCount, m.portK.begin()); ptr += portCount * portCount;
        std::copy(ptr, ptr + stateCount * affineCols, m.stateAffine.begin()); ptr += stateCount * affineCols;
        std::copy(ptr, ptr + stateCount * portCount, m.stateQ.begin()); ptr += stateCount * portCount;
        std::copy(ptr, ptr + m.outAffine.size(), m.outAffine.begin()); ptr += m.outAffine.size();
        std::copy(ptr, ptr + m.outQ.size(), m.outQ.begin());
    }
    
    m.workspaceSize = stateCount + portCount + 10;
    m.initialWorkspace.assign(m.workspaceSize, 0.0);
    m.ok = true;
    return m;
}

double Model::step(std::vector<double>& ws, const double* coeffs, double audioInVolts) const 
{ 
    if (ws.size() < (size_t)workspaceSize) return 0.0;
    int affineCols = 1 + inputCount + stateCount;
    
    const double* pAff = coeffs ? coeffs : portAffine.data();
    const double* pK   = coeffs ? coeffs + portCount * affineCols : portK.data();
    const double* sAff = coeffs ? coeffs + portCount * affineCols + portCount * portCount : stateAffine.data();
    const double* sQ   = coeffs ? coeffs + portCount * affineCols + portCount * portCount + stateCount * affineCols : stateQ.data();
    const double* oAff = coeffs ? coeffs + portCount * affineCols + portCount * portCount + stateCount * affineCols + stateCount * portCount : outAffine.data();
    const double* oQ   = coeffs ? coeffs + portCount * affineCols + portCount * portCount + stateCount * affineCols + stateCount * portCount + outAffine.size() : outQ.data();
    
    std::vector<double> u_s(affineCols, 0.0);
    u_s[0] = 1.0;
    u_s[1] = audioInVolts;
    for (int i = 0; i < stateCount; ++i) u_s[1 + inputCount + i] = ws[i];
    
    std::vector<double> p0(portCount, 0.0);
    for (int p = 0; p < portCount; ++p)
        for (int c = 0; c < affineCols; ++c)
            p0[p] += pAff[p * affineCols + c] * u_s[c];
            
    std::vector<double> p_v = p0;
    std::vector<double> p_i(portCount, 0.0);
    
    if (portCount > 0)
    {
        for (int p = 0; p < portCount; ++p)
            p_v[p] = ws[stateCount + p];
            
        std::vector<double> deriv(portCount, 0.0);
        std::vector<double> current(portCount, 0.0);
        std::vector<double> F(portCount, 0.0);
        std::vector<double> J(portCount * portCount, 0.0);
        
        for (int it = 0; it < maxIterations; ++it)
        {
            double vt = 0.02585;
            double is = 1e-14;
            
            for (int p = 0; p < portCount; ++p)
            {
                if (devices[p].kind == Device::Kind::Diode)
                {
                    p_v[p] = std::max(-100.0, std::min(p_v[p], 2.0));
                    double exp_val = std::exp(p_v[p] / vt);
                    current[p] = is * (exp_val - 1.0);
                    deriv[p] = (is / vt) * exp_val;
                }
            }
            
            double max_abs_F = 0.0;
            for (int p = 0; p < portCount; ++p)
            {
                double k_i = 0.0;
                for (int q = 0; q < portCount; ++q)
                    k_i += pK[p * portCount + q] * current[q];
                F[p] = p_v[p] - p0[p] - k_i;
                max_abs_F = std::max(max_abs_F, std::abs(F[p]));
            }
            
            if (max_abs_F < 1e-6) break;
            
            for (int p = 0; p < portCount; ++p)
            {
                for (int q = 0; q < portCount; ++q)
                {
                    J[p * portCount + q] = (p == q ? 1.0 : 0.0) - pK[p * portCount + q] * deriv[q];
                }
            }
            
            std::vector<double> delta = F;
            bool singular = false;
            // Gaussian elimination with partial pivoting
            for (int c = 0; c < portCount; ++c)
            {
                int pivot = c;
                double max_val = std::abs(J[c * portCount + c]);
                for (int r = c + 1; r < portCount; ++r)
                {
                    if (std::abs(J[r * portCount + c]) > max_val)
                    {
                        max_val = std::abs(J[r * portCount + c]);
                        pivot = r;
                    }
                }
                if (max_val < 1e-12)
                {
                    singular = true;
                    break;
                }
                if (pivot != c)
                {
                    for (int q = c; q < portCount; ++q) std::swap(J[c * portCount + q], J[pivot * portCount + q]);
                    std::swap(delta[c], delta[pivot]);
                }
                for (int r = c + 1; r < portCount; ++r)
                {
                    double factor = J[r * portCount + c] / J[c * portCount + c];
                    for (int q = c; q < portCount; ++q)
                        J[r * portCount + q] -= factor * J[c * portCount + q];
                    delta[r] -= factor * delta[c];
                }
            }
            if (singular) break;
            
            for (int r = portCount - 1; r >= 0; --r)
            {
                for (int c = r + 1; c < portCount; ++c)
                    delta[r] -= J[r * portCount + c] * delta[c];
                delta[r] /= J[r * portCount + r];
            }
            
            for (int p = 0; p < portCount; ++p)
            {
                if (delta[p] > 0.05) delta[p] = 0.05;
                if (delta[p] < -0.05) delta[p] = -0.05;
                p_v[p] -= delta[p];
            }
        }
        
        for (int p = 0; p < portCount; ++p)
        {
            ws[stateCount + p] = p_v[p];
            if (devices[p].kind == Device::Kind::Diode)
            {
                double vt = 0.02585;
                double is = 1e-14;
                p_i[p] = is * (std::exp(p_v[p] / vt) - 1.0);
            }
        }
    }
    
    for (int s = 0; s < stateCount; ++s)
    {
        double s_new = 0.0;
        for (int c = 0; c < affineCols; ++c)
            s_new += sAff[s * affineCols + c] * u_s[c];
        for (int p = 0; p < portCount; ++p)
            s_new += sQ[s * portCount + p] * p_i[p];
        ws[s] = s_new;
    }
    
    double y = 0.0;
    for (int c = 0; c < affineCols; ++c) y += oAff[c] * u_s[c];
    for (int p = 0; p < portCount; ++p) y += oQ[p] * p_i[p];
    
    return y;
}

std::string Model::frustSource() const 
{
    std::ostringstream ss;
    int ws_size = workspaceSize > 0 ? workspaceSize : 1;
    int c_size = coeffSize() > 0 ? coeffSize() : 1;
    ss << "extern fn djehuti_dsp_exp(x: f64) -> f64;\n";
    ss << "extern fn djehuti_dsp_log(x: f64) -> f64;\n";
    ss << "pub fn process_sample(audio_in: f64, ws: Array<f64, " << ws_size << ">, coeffs: Array<f64, " << c_size << ">) -> f64 = {\n";
    
    int affineCols = 1 + inputCount + stateCount;
    
    ss << "    // 1. Affine basis\n";
    int coeff_idx = 0;
    for (int p = 0; p < portCount; ++p)
    {
        ss << "    let mut p0_" << p << " = 0.0;\n";
        for (int c = 0; c < affineCols; ++c)
        {
            std::string term;
            if (c == 0) term = "1.0";
            else if (c == 1) term = "audio_in";
            else term = "ws[" + std::to_string(c - 1 - inputCount) + "]";
            ss << "    p0_" << p << " = p0_" << p << " + coeffs[" << coeff_idx++ << "] * " << term << ";\n";
        }
    }
    
    ss << "\n";
    if (portCount > 0)
    {
        for (int p = 0; p < portCount; ++p)
        {
            ss << "    let mut p_v_" << p << " = ws[" << stateCount + p << "];\n";
            ss << "    let mut p_i_" << p << " = 0.0;\n";
            ss << "    let mut current_" << p << " = 0.0;\n";
            ss << "    let mut deriv_" << p << " = 0.0;\n";
            ss << "    let mut F_" << p << " = 0.0;\n";
            ss << "    let mut delta_" << p << " = 0.0;\n";
        }
        for (int p = 0; p < portCount; ++p)
        {
            for (int q = 0; q < portCount; ++q)
                ss << "    let mut J_" << p << "_" << q << " = 0.0;\n";
        }
        
        ss << "    let mut vt = 0.02585;\n";
        ss << "    let mut is = 0.00000000000001;\n";
        ss << "    let mut it = 0;\n";
        ss << "    let mut exp_val = 0.0;\n";
        ss << "    let mut abs_delta = 1.0;\n";
        
        ss << "    while (it < " << maxIterations << ") {\n";
        
        for (int p = 0; p < portCount; ++p)
        {
            if (devices[p].kind == Device::Kind::Diode)
            {
                ss << "        while (p_v_" << p << " < -100.0) { p_v_" << p << " = -100.0; };\n";
                ss << "        while (p_v_" << p << " > 2.0) { p_v_" << p << " = 2.0; };\n";
                ss << "        exp_val = djehuti_dsp_exp(p_v_" << p << " / vt);\n";
                ss << "        current_" << p << " = is * (exp_val - 1.0);\n";
                ss << "        deriv_" << p << " = (is / vt) * exp_val;\n";
            }
        }
        
        for (int p = 0; p < portCount; ++p)
        {
            ss << "        F_" << p << " = p_v_" << p << " - p0_" << p << ";\n";
            for (int q = 0; q < portCount; ++q)
                ss << "        F_" << p << " = F_" << p << " - coeffs[" << coeff_idx + p * portCount + q << "] * current_" << q << ";\n";
        }
        
        for (int p = 0; p < portCount; ++p)
        {
            for (int q = 0; q < portCount; ++q)
            {
                if (p == q) ss << "        J_" << p << "_" << q << " = 1.0 - coeffs[" << coeff_idx + p * portCount + q << "] * deriv_" << q << ";\n";
                else        ss << "        J_" << p << "_" << q << " = 0.0 - coeffs[" << coeff_idx + p * portCount + q << "] * deriv_" << q << ";\n";
            }
        }
        coeff_idx += portCount * portCount;
        
        for (int c = 0; c < portCount; ++c)
        {
            for (int r = c + 1; r < portCount; ++r)
            {
                ss << "        let mut abs_c = J_" << c << "_" << c << ";\n";
                ss << "        while (abs_c < 0.0) { abs_c = 0.0 - abs_c; };\n";
                ss << "        let mut abs_r = J_" << r << "_" << c << ";\n";
                ss << "        while (abs_r < 0.0) { abs_r = 0.0 - abs_r; };\n";
                
                ss << "        let mut do_swap = 1;\n";
                ss << "        while (abs_r <= abs_c) { do_swap = 0; abs_r = abs_c + 1.0; };\n";
                ss << "        while (do_swap > 0) {\n";
                for (int q = c; q < portCount; ++q)
                {
                    ss << "            let mut tmp_J_" << q << " = J_" << c << "_" << q << ";\n";
                    ss << "            J_" << c << "_" << q << " = J_" << r << "_" << q << ";\n";
                    ss << "            J_" << r << "_" << q << " = tmp_J_" << q << ";\n";
                }
                ss << "            let mut tmp_F = F_" << c << ";\n";
                ss << "            F_" << c << " = F_" << r << ";\n";
                ss << "            F_" << r << " = tmp_F;\n";
                ss << "            do_swap = 0;\n";
                ss << "        };\n";
            }
            
            ss << "        let mut abs_pivot = J_" << c << "_" << c << ";\n";
            ss << "        while (abs_pivot < 0.0) { abs_pivot = 0.0 - abs_pivot; };\n";
            ss << "        while (abs_pivot < 0.000000000001) {\n";
            ss << "            it = " << maxIterations << ";\n";
            ss << "            abs_delta = 0.0;\n";
            ss << "            abs_pivot = 1.0;\n";
            ss << "        };\n";
            
            for (int r = c + 1; r < portCount; ++r)
            {
                ss << "        let mut factor_" << r << "_" << c << " = J_" << r << "_" << c << " / J_" << c << "_" << c << ";\n";
                for (int q = c; q < portCount; ++q)
                    ss << "        J_" << r << "_" << q << " = J_" << r << "_" << q << " - factor_" << r << "_" << c << " * J_" << c << "_" << q << ";\n";
                ss << "        F_" << r << " = F_" << r << " - factor_" << r << "_" << c << " * F_" << c << ";\n";
            }
        }
        
        for (int r = portCount - 1; r >= 0; --r)
        {
            ss << "        delta_" << r << " = F_" << r << ";\n";
            for (int c = r + 1; c < portCount; ++c)
                ss << "        delta_" << r << " = delta_" << r << " - J_" << r << "_" << c << " * delta_" << c << ";\n";
            ss << "        delta_" << r << " = delta_" << r << " / J_" << r << "_" << r << ";\n";
        }
        
        ss << "        abs_delta = 0.0;\n";
        for (int p = 0; p < portCount; ++p)
        {
            ss << "        while (delta_" << p << " > 0.05) { delta_" << p << " = 0.05; };\n";
            ss << "        while (delta_" << p << " < -0.05) { delta_" << p << " = -0.05; };\n";
            ss << "        p_v_" << p << " = p_v_" << p << " - delta_" << p << ";\n";
            
            ss << "        let mut abs_p = delta_" << p << ";\n";
            ss << "        while (abs_p < 0.0) { abs_p = 0.0 - abs_p; };\n";
            ss << "        while (abs_p > abs_delta) { abs_delta = abs_p; };\n";
        }
        
        ss << "        while (abs_delta < 0.000001) { it = " << maxIterations << "; abs_delta = 1.0; };\n";
        ss << "        it = it + 1;\n";
        ss << "    };\n";
        
        for (int p = 0; p < portCount; ++p)
        {
            ss << "    ws[" << stateCount + p << "] = p_v_" << p << ";\n";
            if (devices[p].kind == Device::Kind::Diode)
                ss << "    p_i_" << p << " = is * (djehuti_dsp_exp(p_v_" << p << " / vt) - 1.0);\n";
        }
    }
    
    ss << "\n";
    for (int s = 0; s < stateCount; ++s)
    {
        ss << "    let mut s_new_" << s << " = 0.0;\n";
        for (int c = 0; c < affineCols; ++c)
        {
            std::string term;
            if (c == 0) term = "1.0";
            else if (c == 1) term = "audio_in";
            else term = "ws[" + std::to_string(c - 1 - inputCount) + "]";
            ss << "    s_new_" << s << " = s_new_" << s << " + coeffs[" << coeff_idx++ << "] * " << term << ";\n";
        }
        for (int p = 0; p < portCount; ++p)
        {
            ss << "    s_new_" << s << " = s_new_" << s << " + coeffs[" << coeff_idx++ << "] * p_i_" << p << ";\n";
        }
    }
    
    ss << "\n";
    ss << "    let mut y = 0.0;\n";
    for (int c = 0; c < affineCols; ++c)
    {
        std::string term;
        if (c == 0) term = "1.0";
        else if (c == 1) term = "audio_in";
        else term = "ws[" + std::to_string(c - 1 - inputCount) + "]";
        ss << "    y = y + coeffs[" << coeff_idx++ << "] * " << term << ";\n";
    }
    for (int p = 0; p < portCount; ++p)
    {
        ss << "    y = y + coeffs[" << coeff_idx++ << "] * p_i_" << p << ";\n";
    }

    for (int s = 0; s < stateCount; ++s)
        ss << "    ws[" << s << "] = s_new_" << s << ";\n";
        
    
    ss << "    y\n";
    ss << "}\n";
    return ss.str();
}

std::vector<std::string> Model::requiredHostFunctions() { return { "djehuti_dsp_exp", "djehuti_dsp_log" }; }
std::string Model::describe() const { return "Audio DSP Pipeline"; }
std::string frustNumber(double value) { std::ostringstream oss; oss << value; return oss.str(); }

int Model::coeffSize() const
{
    int affineCols = 1 + inputCount + stateCount;
    return portCount * affineCols + portCount * portCount + 
           stateCount * affineCols + stateCount * portCount + 
           outAffine.size() + outQ.size();
}

std::vector<double> Model::computeLiveCoefficients(const std::unordered_map<std::string, double>& liveParams) const
{
    std::vector<double> out(coeffSize(), 0.0);
    int L = livePortCount;
    int N = portCount;
    int totalP = L + N;
    int affCols = 1 + inputCount + stateCount;
    
    if (L == 0) {
        auto ptr = out.begin();
        std::copy(baseAffine.begin(), baseAffine.begin() + N * affCols, ptr); ptr += N * affCols;
        std::copy(baseK.begin(), baseK.begin() + N * N, ptr); ptr += N * N;
        std::copy(stateAffine.begin(), stateAffine.end(), ptr); ptr += stateCount * affCols;
        std::copy(baseStateQ.begin(), baseStateQ.begin() + stateCount * N, ptr); ptr += stateCount * N;
        std::copy(outAffine.begin(), outAffine.end(), ptr); ptr += affCols;
        std::copy(baseOutQ.begin(), baseOutQ.begin() + N, ptr);
        return out;
    }
    
    std::vector<double> CV(L, 0.0), CI(L, 0.0);
    for (int j = 0; j < L; ++j) {
        const auto& lp = livePorts[j];
        double val = 0.5;
        auto it = liveParams.find(lp.paramId);
        if (it != liveParams.end()) val = it->second;
        
        if (lp.kind == LivePort::Kind::Switch) {
            if (val > 0.5) { CV[j] = 1.0; CI[j] = 0.0; } // Closed: V = 0
            else           { CV[j] = 0.0; CI[j] = 1.0; } // Open: I = 0
        } else {
            double pos = std::max(0.0, std::min(1.0, val));
            double R = lp.baseResistance * (lp.isWiperToPin2 ? (1.0 - pos) : pos);
            if (R < 1e-9) { 
                CV[j] = 1.0; CI[j] = 0.0; // Short circuit: V = 0
            } else {
                CV[j] = 1.0; CI[j] = -R;  // Resistor: V - R*I = 0
            }
        }
    }
    
    std::vector<std::vector<double>> H(L, std::vector<double>(L, 0.0));
    for (int r = 0; r < L; ++r) {
        for (int c = 0; c < L; ++c) {
            H[r][c] = CV[r] * baseK[r * totalP + c] + (r == c ? CI[r] : 0.0);
        }
    }
    
    std::vector<std::vector<double>> M(L, std::vector<double>(L, 0.0));
    for (int col = 0; col < L; ++col) {
        std::vector<double> rhs(L, 0.0);
        rhs[col] = -CV[col];
        std::vector<double> x;
        if (solveLinear(H, rhs, x)) {
            for (int r = 0; r < L; ++r) M[r][col] = x[r];
        } else {
            // Singular! Should not happen with our regularization. 
            // Just return zeroes or base if we somehow hit this.
        }
    }
    
    // Now compute the reduced matrices!
    // K'_{NN} = K_{NN} + K_{NL} * M * K_{LN}
    // P'_{0N} = P_{0N} + K_{NL} * M * P_{0L}
    auto get_M_KLN = [&](int r, int c) {
        double sum = 0.0;
        for (int k = 0; k < L; ++k) sum += M[r][k] * baseK[k * totalP + (L + c)];
        return sum;
    };
    auto get_M_P0L = [&](int r, int c) {
        double sum = 0.0;
        for (int k = 0; k < L; ++k) sum += M[r][k] * baseAffine[k * affCols + c];
        return sum;
    };
    
    std::vector<std::vector<double>> M_KLN(L, std::vector<double>(N, 0.0));
    std::vector<std::vector<double>> M_P0L(L, std::vector<double>(affCols, 0.0));
    for (int r = 0; r < L; ++r) {
        for (int c = 0; c < N; ++c) M_KLN[r][c] = get_M_KLN(r, c);
        for (int c = 0; c < affCols; ++c) M_P0L[r][c] = get_M_P0L(r, c);
    }
    
    auto ptr = out.begin();
    
    // 1. portAffine (P'_{0N})
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < affCols; ++c) {
            double sum = baseAffine[(L + r) * affCols + c];
            for (int k = 0; k < L; ++k) sum += baseK[(L + r) * totalP + k] * M_P0L[k][c];
            *ptr++ = sum;
        }
    }
    
    // 2. portK (K'_{NN})
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            double sum = baseK[(L + r) * totalP + (L + c)];
            for (int k = 0; k < L; ++k) sum += baseK[(L + r) * totalP + k] * M_KLN[k][c];
            *ptr++ = sum;
        }
    }
    
    // 3. stateAffine (A'_{S})
    for (int r = 0; r < stateCount; ++r) {
        for (int c = 0; c < affCols; ++c) {
            double sum = stateAffine[r * affCols + c];
            for (int k = 0; k < L; ++k) sum += baseStateQ[r * totalP + k] * M_P0L[k][c];
            *ptr++ = sum;
        }
    }
    
    // 4. stateQ (Q'_{SN})
    for (int r = 0; r < stateCount; ++r) {
        for (int c = 0; c < N; ++c) {
            double sum = baseStateQ[r * totalP + (L + c)];
            for (int k = 0; k < L; ++k) sum += baseStateQ[r * totalP + k] * M_KLN[k][c];
            *ptr++ = sum;
        }
    }
    
    // 5. outAffine (A'_{Y})
    for (int c = 0; c < affCols; ++c) {
        double sum = outAffine[c];
        for (int k = 0; k < L; ++k) sum += baseOutQ[k] * M_P0L[k][c];
        *ptr++ = sum;
    }
    
    // 6. outQ (Q'_{YN})
    for (int c = 0; c < N; ++c) {
        double sum = baseOutQ[L + c];
        for (int k = 0; k < L; ++k) sum += baseOutQ[k] * M_KLN[k][c];
        *ptr++ = sum;
    }
    
    return out;
}

}

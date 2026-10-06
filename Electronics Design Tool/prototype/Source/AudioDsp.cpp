#include "AudioDsp.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace audio_dsp
{

static bool invertMatrix(std::vector<std::vector<double>>& a)
{
    int n = (int)a.size();
    if (n == 0) return true;
    std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) inv[i][i] = 1.0;
    
    for (int i = 0; i < n; ++i)
    {
        int pivot = i;
        double best = std::abs(a[i][i]);
        for (int r = i + 1; r < n; ++r)
            if (std::abs(a[r][i]) > best) { best = std::abs(a[r][i]); pivot = r; }
            
        if (best < 1e-12) return false;
        
        std::swap(a[i], a[pivot]);
        std::swap(inv[i], inv[pivot]);
        
        double p = a[i][i];
        for (int c = 0; c < n; ++c) { a[i][c] /= p; inv[i][c] /= p; }
        
        for (int r = 0; r < n; ++r)
        {
            if (r == i) continue;
            double f = a[r][i];
            for (int c = 0; c < n; ++c)
            {
                a[r][c] -= f * a[i][c];
                inv[r][c] -= f * inv[i][c];
            }
        }
    }
    a = inv;
    return true;
}

Model build(const circuit_sim::Circuit& circuit, const Config& config)
{
    Model m;
    m.sampleRate = config.sampleRate;
    m.inputVolts = config.inputVolts;
    m.outputFullScaleVolts = config.outputFullScaleVolts;
    
    // layout
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
            portCount += 1;
        }
        else if (e.type == circuit_sim::Element::Type::Npn || e.type == circuit_sim::Element::Type::Pnp)
        {
            Device d;
            d.kind = Device::Kind::Bjt;
            d.sign = e.type == circuit_sim::Element::Type::Npn ? 1.0 : -1.0;
            d.element = (int)i;
            d.port = portCount;
            d.ports = 2;
            d.parameters = e;
            m.devices.push_back(d);
            portCount += 2;
        }
        else if (e.type == circuit_sim::Element::Type::Nmos || e.type == circuit_sim::Element::Type::Pmos)
        {
            Device d;
            d.kind = Device::Kind::Mos;
            d.sign = e.type == circuit_sim::Element::Type::Nmos ? 1.0 : -1.0;
            d.element = (int)i;
            d.port = portCount;
            d.ports = 2;
            d.parameters = e;
            m.devices.push_back(d);
            portCount += 2;
        }
        else if (e.type == circuit_sim::Element::Type::OpAmp && e.opamp.limited)
        {
            Device d;
            d.kind = Device::Kind::OpAmp;
            d.element = (int)i;
            d.port = portCount;
            d.ports = 1; // Simplification for now, rails are not ports
            d.parameters = e;
            m.devices.push_back(d);
            portCount += 1;
        }
    }
    m.portCount = portCount;
    
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
                break;
            }
            case circuit_sim::Element::Type::Inductor:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                addA(br, br, -2.0 * e.value * config.sampleRate);
                stateIdx[i] = stateCount++;
                break;
            }
            case circuit_sim::Element::Type::VoltageSource:
            {
                addA(p, br, 1.0); addA(n, br, -1.0); addA(br, p, 1.0); addA(br, n, -1.0);
                break;
            }
            case circuit_sim::Element::Type::CurrentSource:
                break; // RHS only
            case circuit_sim::Element::Type::OpAmp:
                if (!e.opamp.limited)
                {
                    int out = idx(e.nodes[2]);
                    int inP = idx(e.nodes[0]);
                    int inN = idx(e.nodes[1]);
                    addA(out, br, 1.0);
                    addA(br, out, 1.0);
                    addA(br, inP, -e.opamp.gain);
                    addA(br, inN, e.opamp.gain);
                }
                else
                {
                    int out = idx(e.nodes[2]);
                    addA(out, br, 1.0);
                    addA(br, out, 1.0);
                }
                break;
            default:
                break;
        }
    }
    m.stateCount = stateCount;
    
    if (!invertMatrix(A))
    {
        m.ok = false;
        m.error = "Singular circuit matrix.";
        return m;
    }
    
    m.ok = true;
    return m;
}

double Model::step(std::vector<double>& ws, double audioInVolts) const { return 0.0; }
std::string Model::frustSource() const {
    return "pub fn process_sample(audio_in: f64, ws: Array<f64, " + std::to_string(workspaceSize > 0 ? workspaceSize : 1) + ">) -> f64 { 0.0 }\n";
}
std::vector<std::string> Model::requiredHostFunctions() { return { "djehuti_dsp_exp", "djehuti_dsp_log" }; }
std::string Model::describe() const { return "Audio DSP Pipeline"; }
std::string frustNumber(double value) { std::ostringstream oss; oss << value; return oss.str(); }

}

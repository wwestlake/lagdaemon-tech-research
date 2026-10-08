#include "FrustAudioGenerator.h"
#include <vector>
#include <cmath>
#include <algorithm>

juce::String FrustAudioGenerator::generate(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode, Method method)
{
    if (method == Method::Auto)
    {
        method = isStrictlyLinear(circuit) ? Method::StateSpace : Method::MNA;
    }

    if (method == Method::StateSpace)
    {
        return generateStateSpace(circuit, audioInputNode, audioOutputNode);
    }
    else
    {
        return generateMNA(circuit, audioInputNode, audioOutputNode);
    }
}

bool FrustAudioGenerator::isStrictlyLinear(const circuit_sim::Circuit& circuit)
{
    for (const auto& el : circuit.elements())
    {
        switch (el.type)
        {
            case circuit_sim::Element::Type::Diode:
            case circuit_sim::Element::Type::Npn:
            case circuit_sim::Element::Type::Pnp:
            case circuit_sim::Element::Type::Nmos:
            case circuit_sim::Element::Type::Pmos:
                return false;
            case circuit_sim::Element::Type::OpAmp:
                if (el.opamp.limited) return false; // Clipping op-amp is non-linear
                break;
            default:
                break;
        }
    }
    return true;
}

juce::String FrustAudioGenerator::generateMNA(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode)
{
    const double dt = 1.0 / 48000.0;
    
    // 1. Identify MNA layout
    int numNodes = circuit.nodeCount();
    int N = numNodes - 1; // node voltages (1 to N)
    
    std::vector<int> branchIdx(circuit.elements().size(), -1);
    for (size_t i = 0; i < circuit.elements().size(); ++i) {
        auto type = circuit.elements()[i].type;
        if (type == circuit_sim::Element::Type::VoltageSource ||
            type == circuit_sim::Element::Type::Inductor ||
            type == circuit_sim::Element::Type::Vcvs ||
            type == circuit_sim::Element::Type::Ccvs ||
            type == circuit_sim::Element::Type::OpAmp) 
        {
            branchIdx[i] = N++;
        }
    }
    
    if (N == 0) return "pub fn process_sample(audio_in: f64) -> f64 = { audio_in * 0.5 }\n";

    // 2. Build constant Conductance Matrix A
    std::vector<std::vector<double>> A(N, std::vector<double>(N, 0.0));
    
    auto add = [&](int r, int c, double v) {
        if (r >= 0 && c >= 0) A[r][c] += v;
    };
    auto nodeIdx = [&](int n) { return n - 1; };

    // Gmin to prevent singular matrix
    for(int i=0; i<numNodes-1; ++i) A[i][i] += 1e-12;

    int audioSrcBranch = -1;
    int audioOutNode = numNodes > 1 ? 1 : 0; // Default to node 1

    for (size_t i = 0; i < circuit.elements().size(); ++i) {
        const auto& e = circuit.elements()[i];
        int p = nodeIdx(e.nodes[0]);
        int n = nodeIdx(e.nodes[1]);
        
        if (e.type == circuit_sim::Element::Type::Resistor) {
            double g = 1.0 / std::max(1e-9, e.value);
            add(p, p, g); add(n, n, g);
            add(p, n, -g); add(n, p, -g);
        }
        else if (e.type == circuit_sim::Element::Type::Capacitor) {
            double g = 2.0 * e.value / dt;
            add(p, p, g); add(n, n, g);
            add(p, n, -g); add(n, p, -g);
        }
        else if (e.type == circuit_sim::Element::Type::Inductor) {
            int br = branchIdx[i];
            add(p, br, 1.0); add(n, br, -1.0);
            add(br, p, 1.0); add(br, n, -1.0);
            add(br, br, -2.0 * e.value / dt);
        }
        else if (e.type == circuit_sim::Element::Type::VoltageSource) {
            int br = branchIdx[i];
            add(p, br, 1.0); add(n, br, -1.0);
            add(br, p, 1.0); add(br, n, -1.0);
            if (audioSrcBranch < 0) audioSrcBranch = br; // pick first source as audio input
        }
    }

    // 3. Invert A using Gauss-Jordan
    std::vector<std::vector<double>> inv(N, std::vector<double>(N, 0.0));
    for (int i=0; i<N; ++i) inv[i][i] = 1.0;
    
    for (int i=0; i<N; ++i) {
        int max_r = i;
        for (int r=i+1; r<N; ++r) {
            if (std::abs(A[r][i]) > std::abs(A[max_r][i])) max_r = r;
        }
        std::swap(A[i], A[max_r]);
        std::swap(inv[i], inv[max_r]);
        
        double p = A[i][i];
        if (std::abs(p) < 1e-15) p = p >= 0 ? 1e-15 : -1e-15;
        
        for (int c=0; c<N; ++c) { A[i][c] /= p; inv[i][c] /= p; }
        
        for (int r=0; r<N; ++r) {
            if (r != i) {
                double factor = A[r][i];
                for (int c=0; c<N; ++c) {
                    A[r][c] -= factor * A[i][c];
                    inv[r][c] -= factor * inv[i][c];
                }
            }
        }
    }

    // 4. Generate Frust Code
    juce::String code;
    code << "// --- Frust MNA Audio DSP ---\n";
    
    // State variables
    for (size_t i = 0; i < circuit.elements().size(); ++i) {
        const auto& e = circuit.elements()[i];
        if (e.type == circuit_sim::Element::Type::Capacitor || e.type == circuit_sim::Element::Type::Inductor) {
            code << "let mut state_" << (int)i << ": f64 = 0.0;\n";
        }
    }
    code << "\n";

    code << "pub fn process_sample(audio_in: f64) -> f64 = {\n";
    
    // b vector formulation
    for (int i=0; i<N; ++i) {
        code << "    let b_" << i << ": f64 = ";
        juce::String terms;
        
        if (i == audioSrcBranch) {
            terms << "audio_in";
        }
        
        for (size_t el = 0; el < circuit.elements().size(); ++el) {
            const auto& e = circuit.elements()[el];
            int p = nodeIdx(e.nodes[0]);
            int n = nodeIdx(e.nodes[1]);
            
            if (e.type == circuit_sim::Element::Type::Capacitor) {
                if (i == p) terms << (terms.isEmpty() ? "" : " + ") << "state_" << (int)el;
                if (i == n) terms << (terms.isEmpty() ? "" : " - ") << "state_" << (int)el;
            }
            else if (e.type == circuit_sim::Element::Type::Inductor) {
                if (i == branchIdx[el]) terms << (terms.isEmpty() ? "" : " + ") << "state_" << (int)el;
            }
        }
        
        if (terms.isEmpty()) code << "0.0;\n";
        else code << terms << ";\n";
    }
    code << "\n";
    
    // Matrix multiplication x = inv * b
    for (int i=0; i<N; ++i) {
        code << "    let x_" << i << ": f64 = ";
        juce::String terms;
        for (int c=0; c<N; ++c) {
            if (std::abs(inv[i][c]) > 1e-12) {
                if (!terms.isEmpty() && inv[i][c] > 0) terms << " + ";
                else if (inv[i][c] < 0) terms << " - ";
                terms << juce::String(std::abs(inv[i][c]), 12) << " * b_" << c;
            }
        }
        if (terms.isEmpty()) code << "0.0;\n";
        else code << terms << ";\n";
    }
    code << "\n";

    // State updates
    for (size_t el = 0; el < circuit.elements().size(); ++el) {
        const auto& e = circuit.elements()[el];
        int p = nodeIdx(e.nodes[0]);
        int n = nodeIdx(e.nodes[1]);
        
        juce::String xp = p >= 0 ? "x_" + juce::String(p) : "0.0";
        juce::String xn = n >= 0 ? "x_" + juce::String(n) : "0.0";
        
        if (e.type == circuit_sim::Element::Type::Capacitor) {
            double g = 2.0 * e.value / dt;
            code << "    state_" << (int)el << " = (0.0 - state_" << (int)el << ") + " 
                 << juce::String(2.0 * g, 12) << " * (" << xp << " - " << xn << ");\n";
        }
        else if (e.type == circuit_sim::Element::Type::Inductor) {
            int br = branchIdx[el];
            double r = 2.0 * e.value / dt;
            code << "    state_" << (int)el << " = (0.0 - state_" << (int)el << ") + " 
                 << juce::String(2.0 * r, 12) << " * x_" << br << ";\n";
        }
    }
    
    if (audioOutputNode > 0 && nodeIdx(audioOutputNode) < N) {
        code << "    x_" << nodeIdx(audioOutputNode) << "\n";
    } else if (audioOutNode > 0 && nodeIdx(audioOutNode) < N) {
        code << "    x_" << nodeIdx(audioOutNode) << "\n";
    } else {
        code << "    0.0\n";
    }
    code << "}\n";

    return code;
}

juce::String FrustAudioGenerator::generateStateSpace(const circuit_sim::Circuit& circuit, int audioInputNode, int audioOutputNode)
{
    juce::String code;
    code << "// --- Frust State-Space Audio DSP ---\n";
    code << "pub fn process_sample(audio_in: f64) -> f64 =\n";
    code << "{\n";
    code << "    // State space: x[n+1] = A x[n] + B u[n]\n";
    code << "    // y[n] = C x[n] + D u[n]\n";
    code << "    audio_in\n";
    code << "}\n";
    return code;
}


#include "../../Source/AudioDsp.h"
#include "../../Source/CircuitSolver.h"
#include "../../Source/FrustEngine.h"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

int failures = 0;

void check(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "FAIL: " << msg << std::endl;
        failures++;
    }
}

void checkNear(double actual, double expected, double tolerance, const std::string& msg) {
    if (std::isnan(actual) || std::isinf(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << msg << " (Expected " << expected << ", got " << actual << ")" << std::endl;
        failures++;
    }
}

void compareStepAndFrust(const audio_dsp::Model& model, const std::vector<double>& inputs, const std::string& testName) {
    frust_engine::Engine engine;
    
    std::string code = frust_engine::manifestLine("audio_dsp", "Generated DSP", {"djehuti_dsp_exp", "djehuti_dsp_log"}) + "\n" + model.frustSource();
    auto result = engine.load("audio_dsp", code);
    if (!result.ok) {
        std::cerr << "FRUST SOURCE:\n" << code << "\n";
    }
    check(result.ok, testName + ": Frust compilation failed:\n" + result.report());
    if (!result.ok) return;
    
    typedef double (*ProcessFn)(double, double*);
    auto* fn = reinterpret_cast<ProcessFn>(engine.function("audio_dsp", "process_sample"));
    check(fn != nullptr, testName + ": Could not find process_sample function");
    if (!fn) return;

    std::vector<double> ws_cpp = model.initialWorkspace;
    if (ws_cpp.size() < (size_t)model.workspaceSize) ws_cpp.resize(model.workspaceSize, 0.0);
    
    std::vector<double> ws_frust = model.initialWorkspace;
    if (ws_frust.size() < (size_t)model.workspaceSize) ws_frust.resize(model.workspaceSize, 0.0);

    for (size_t i = 0; i < inputs.size(); ++i) {
        double out_cpp = model.step(ws_cpp, inputs[i]);
        double out_frust = fn(inputs[i], ws_frust.data());
        
        checkNear(out_frust, out_cpp, 1e-6, testName + ": Frust output differs from C++ output at sample " + std::to_string(i));
        
        for (size_t w = 0; w < ws_cpp.size(); ++w) {
            checkNear(ws_frust[w], ws_cpp[w], 1e-6, testName + ": Frust workspace differs at state " + std::to_string(w) + " on sample " + std::to_string(i));
        }
    }
}

void testResistiveDivider() {
    circuit_sim::Circuit c;
    int v1 = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", v1, 0, {circuit_sim::Waveform::Kind::Dc, 1.0}); // V1 = 1V
    c.addResistor("R1", v1, out, 1000.0);
    c.addResistor("R2", out, 0, 1000.0);

    circuit_sim::Options opts;
    auto op = circuit_sim::solveOperatingPoint(c, opts);
    check(op.ok, "Resistive: CircuitSolver DC failed");
    checkNear(op.voltages[out], 0.5, 1e-9, "Resistive: CircuitSolver Analytical Mismatch");

    audio_dsp::Config config;
    config.audioInputElement = 0; // V1
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    auto model = audio_dsp::build(c, config);
    check(model.ok, "Resistive: AudioDsp build failed");
    if (!model.ok) return;

    std::vector<double> ws(std::max(1, model.workspaceSize), 0.0);
    double audioOut = model.step(ws, 1.0);
    checkNear(audioOut, 0.5, 1e-9, "Resistive: AudioDsp step Analytical Mismatch");
    
    compareStepAndFrust(model, {1.0, -1.0, 0.0}, "Resistive");
}

void testRCTransient() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addResistor("R1", in, out, 1000.0);
    c.addCapacitor("C1", out, 0, 1e-6); // tau = 1ms

    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    check(model.ok, "RC: AudioDsp build failed");
    if (!model.ok) return;

    std::vector<double> ws(model.workspaceSize, 0.0);
    double Sk = 0.0;
    std::vector<double> inputs;
    for (int i = 1; i <= 5; ++i) {
        inputs.push_back(1.0);
        double expected = (0.001 * 1.0 + Sk) / 0.097;
        Sk = 0.192 * expected - Sk;
        double audioOut = model.step(ws, 1.0);
        checkNear(audioOut, expected, 1e-6, "RC: AudioDsp deviates from analytical step " + std::to_string(i));
    }
    
    compareStepAndFrust(model, inputs, "RC");
}

void testNonlinearDiode() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addResistor("R1", in, out, 100.0);
    c.addDiode("D1", out, 0); // Diode clipping

    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    check(model.ok, "Diode: AudioDsp build failed");
    if (!model.ok) return;

    std::vector<double> ws(model.workspaceSize, 0.0);
    double audioOut = model.step(ws, 5.0);
    check(audioOut > 0.5 && audioOut < 1.0, "Diode: Expected clipped output, got " + std::to_string(audioOut));
    
    compareStepAndFrust(model, {5.0, -5.0, 10.0, 0.0}, "Diode");
}

void testMultiPortNonlinear() {
    // Two diodes back to back (clipper)
    circuit_sim::Circuit c;
    int in = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addResistor("R1", in, out, 1000.0);
    c.addDiode("D1", out, 0);
    c.addDiode("D2", 0, out);

    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    check(model.ok, "MultiPort: AudioDsp build failed");
    if (!model.ok) return;

    check(model.portCount == 2, "MultiPort: Expected 2 ports");
    compareStepAndFrust(model, {5.0, -5.0, 1.0, -1.0}, "MultiPort");
}

void testCoupledNonlinear() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addResistor("R1", in, out, 1000.0);
    c.addDiode("D1", out, 0);
    c.addDiode("D2", out, 0); // parallel!

    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    check(model.ok, "Coupled: AudioDsp build failed");
    if (!model.ok) return;

    // Independent reference: D1+D2 is equivalent to a single diode with 2x Is.
    double v = 0.6;
    double vt = 0.02585;
    double is = 1e-14;
    for (int i = 0; i < 40; ++i) {
        double current = 2.0 * is * (std::exp(v / vt) - 1.0);
        double deriv = 2.0 * (is / vt) * std::exp(v / vt);
        double f = v - 5.0 + 1000.0 * current;
        double df = 1.0 + 1000.0 * deriv;
        v -= f / df;
    }
    double expected_out = v;

    std::vector<double> ws(model.workspaceSize, 0.0);
    double out_cpp = model.step(ws, 5.0);

    checkNear(out_cpp, expected_out, 1e-6, "Coupled: C++ solver must match independent 2*Is reference");
    compareStepAndFrust(model, {5.0}, "Coupled");
}

void testPivotingAndSingularity() {
    audio_dsp::Model m;
    m.ok = true;
    m.workspaceSize = 2; // 2 ports
    m.stateCount = 0;
    m.inputCount = 1;
    m.portCount = 2;
    m.portAffine = {0.0, 0.0, 0.0, 0.0};
    // To FORCE a row swap in step 0, we need J_00 to be very small, and J_10 to be large.
    // J = I - K * diag(deriv).
    // Let's just make the diodes behave like linear resistors by faking the initial state 
    // or just relying on the first step.
    // At v=0, deriv = Is/Vt = 1e-14 / 0.02585 ~= 3.8e-13.
    // To make J_00 = 0, we need K_00 * deriv = 1 => K_00 = 1 / 3.8e-13 = 2.585e12!
    // Let's set K_00 = 2.585e12. Then J_00 = 1 - 1 = 0!
    // And set K_10 = 1e13 so J_10 is large.
    m.portK = { 2.585e12, 1.0, 
                1e13,     1.0 };
    m.outAffine = {0.0, 0.0};
    m.outQ = {1.0, 1.0};
    
    audio_dsp::Device d;
    d.kind = audio_dsp::Device::Kind::Diode;
    d.port = 0; d.ports = 1;
    m.devices.push_back(d);
    d.port = 1; d.ports = 1;
    m.devices.push_back(d);
    
    std::vector<double> ws(m.workspaceSize, 0.0);
    // This should NOT crash with divide-by-zero. It should swap rows or hit singularity logic.
    double out_cpp = m.step(ws, 1.0);
    
    // Test Frust compilation and execution
    compareStepAndFrust(m, {1.0}, "Pivoting");
}

void testFailurePaths() {
    // 1. Floating node (Singular Matrix)
    {
        circuit_sim::Circuit c;
        int v1 = c.addNode();
        int floating = c.addNode();
        c.addVoltageSource("V1", v1, 0, {circuit_sim::Waveform::Kind::Dc, 1.0});
        c.addResistor("R1", v1, floating, 1000.0);
        c.addCapacitor("C1", floating, c.addNode(), 1e-6); // other side floating!

        audio_dsp::Config config;
        config.audioInputElement = 0;
        config.audioOutputNode = floating;
        config.sampleRate = 48000.0;
        
        auto model = audio_dsp::build(c, config);
        // Either GMIN fixes it, or it returns !ok. We ensure no NaN/Inf.
        if (!model.ok) {
            check(model.error.find("Singular") != std::string::npos, "Failure Path: Expected Singular error message");
        } else {
            std::vector<double> ws(model.workspaceSize, 0.0);
            double val = model.step(ws, 1.0);
            check(!std::isnan(val) && !std::isinf(val), "Failure Path: Floating node resulted in NaN/Inf");
        }
    }
}

int main() {
    testResistiveDivider();
    testRCTransient();
    testNonlinearDiode();
    testMultiPortNonlinear();
    testCoupledNonlinear();
    testPivotingAndSingularity();
    testFailurePaths();

    if (failures == 0) {
        std::cout << "All AudioDspTests passed." << std::endl;
        return 0;
    } else {
        std::cerr << failures << " tests failed." << std::endl;
        return 1;
    }
}

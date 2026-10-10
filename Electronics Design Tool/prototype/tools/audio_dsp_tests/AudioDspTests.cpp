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
    
    typedef double (*ProcessFn)(double, double*, double*);
    auto* fn = reinterpret_cast<ProcessFn>(engine.function("audio_dsp", "process_sample"));
    check(fn != nullptr, testName + ": Could not find process_sample function");
    if (!fn) return;

    std::vector<double> ws_cpp = model.initialWorkspace;
    if (ws_cpp.size() < (size_t)model.workspaceSize) ws_cpp.resize(model.workspaceSize, 0.0);
    
    std::vector<double> ws_frust = model.initialWorkspace;
    if (ws_frust.size() < (size_t)model.workspaceSize) ws_frust.resize(model.workspaceSize, 0.0);

    std::unordered_map<std::string, double> emptyParams;
    std::vector<double> coeffs = model.computeLiveCoefficients(emptyParams);
    if (coeffs.empty()) coeffs.push_back(0.0);

    for (size_t i = 0; i < inputs.size(); ++i) {
        double out_cpp = model.step(ws_cpp, coeffs.data(), inputs[i]);
        double out_frust = fn(inputs[i], ws_frust.data(), coeffs.data());
        
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
    double audioOut = model.step(ws, nullptr, 1.0);
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
        double audioOut = model.step(ws, nullptr, 1.0);
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
    double audioOut = model.step(ws, nullptr, 5.0);
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
    double out_cpp = model.step(ws, nullptr, 5.0);

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
    double out_cpp = m.step(ws, nullptr, 1.0);
    check(!std::isnan(out_cpp) && !std::isinf(out_cpp), "Pivoting: C++ solver returned NaN/Inf");
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
            double val = model.step(ws, nullptr, 1.0);
            check(!std::isnan(val) && !std::isinf(val), "Failure Path: Floating node resulted in NaN/Inf");
        }
    }
}

void testCoupledLivePorts() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int mid = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addVariableResistor("POT_A", in, mid, 1000.0, "pot_pos", false); // R = pos * 1000
    c.addVariableResistor("POT_B", mid, out, 1000.0, "pot_pos", true); // R = (1-pos) * 1000
    c.addSwitch("SW1", out, 0, "sw_state");
    
    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    if (!model.ok) return;

    std::vector<double> ws_cpp(model.workspaceSize > 0 ? model.workspaceSize : 1, 0.0);
    
    // Test SW1 closed, pot at 0.5
    {
        std::unordered_map<std::string, double> params = {{"pot_pos", 0.5}, {"sw_state", 1.0}};
        std::vector<double> coeffs = model.computeLiveCoefficients(params);
        double out_cpp = model.step(ws_cpp, coeffs.data(), 1.0);
        checkNear(out_cpp, 0.0, 1e-5, "Coupled: out should be 0V when switch is closed");
    }

    // Test SW1 open, pot at 0.5
    {
        std::unordered_map<std::string, double> params = {{"pot_pos", 0.5}, {"sw_state", 0.0}};
        std::vector<double> coeffs = model.computeLiveCoefficients(params);
        double out_cpp = model.step(ws_cpp, coeffs.data(), 1.0);
        checkNear(out_cpp, 1.0, 1e-5, "Coupled: out should be 1V when switch is open and no load");
    }

    // Test pot exact endpoint pos=1.0 (POT_A = 1000, POT_B = 0) with load (switch doesn't matter if we just test POT_B shorting)
    // Actually wait, if SW is open, no current flows, so out is 1.0.
}

void testContinuousSweep() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addVariableResistor("VR1", in, out, 1000.0, "vr_pos", true);
    c.addResistor("R1", out, 0, 1000.0);
    
    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    if (!model.ok) return;

    frust_engine::Engine engine;
    std::string code = frust_engine::manifestLine("audio_dsp", "Generated DSP", {"djehuti_dsp_exp", "djehuti_dsp_log"}) + "\n" + model.frustSource();
    auto result = engine.load("audio_dsp", code);
    check(result.ok, "Sweep: Frust compilation failed");
    
    typedef double (*ProcessFn)(double, double*, double*);
    auto* fn = reinterpret_cast<ProcessFn>(engine.function("audio_dsp", "process_sample"));
    if (!fn) return;

    std::vector<double> ws_cpp(model.workspaceSize > 0 ? model.workspaceSize : 1, 0.0);
    std::vector<double> ws_frust(model.workspaceSize > 0 ? model.workspaceSize : 1, 0.0);

    for (int i = 0; i <= 10; ++i) {
        double pos = i / 10.0;
        std::unordered_map<std::string, double> params = {{"vr_pos", pos}};
        std::vector<double> coeffs = model.computeLiveCoefficients(params);

        double out_cpp = model.step(ws_cpp, coeffs.data(), 1.0);
        double out_frust = fn(1.0, ws_frust.data(), coeffs.data());
        
        checkNear(out_frust, out_cpp, 1e-6, "Sweep: Frust differs at pos " + std::to_string(pos));
        
        // isWiperToPin2 == true -> R = base * (1.0 - pos)
        double Rvr = 1000.0 * (1.0 - pos);
        double expected = 1.0 * 1000.0 / (1000.0 + Rvr);
        checkNear(out_cpp, expected, 1e-5, "Sweep: C++ differs from expected at pos " + std::to_string(pos));
    }
}

void testStateContinuity() {
    circuit_sim::Circuit c;
    int in = c.addNode();
    int mid = c.addNode();
    int out = c.addNode();
    c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
    c.addSwitch("SW1", in, mid, "sw_state");
    c.addResistor("R1", mid, out, 1000.0);
    c.addCapacitor("C1", out, 0, 1e-3); // large cap
    
    audio_dsp::Config config;
    config.audioInputElement = 0;
    config.audioOutputNode = out;
    config.sampleRate = 48000.0;
    
    auto model = audio_dsp::build(c, config);
    if (!model.ok) return;
    
    std::vector<double> ws_cpp(model.workspaceSize > 0 ? model.workspaceSize : 1, 0.0);
    
    std::unordered_map<std::string, double> params_closed = {{"sw_state", 1.0}};
    std::unordered_map<std::string, double> params_open = {{"sw_state", 0.0}};
    
    std::vector<double> coeffs_closed = model.computeLiveCoefficients(params_closed);
    std::vector<double> coeffs_open = model.computeLiveCoefficients(params_open);
    
    double last_out = 0.0;
    for (int i = 0; i < 1000; ++i) {
        last_out = model.step(ws_cpp, coeffs_closed.data(), 1.0);
    }
    check(last_out > 0.01, "StateContinuity: Capacitor didn't charge");
    
    double out_after_open = model.step(ws_cpp, coeffs_open.data(), 1.0);
    checkNear(out_after_open, last_out, 1e-3, "StateContinuity: Capacitor lost charge instantly upon switch open");
    
    for (int i = 0; i < 100; ++i) {
        out_after_open = model.step(ws_cpp, coeffs_open.data(), 1.0);
    }
    checkNear(out_after_open, last_out, 1e-3, "StateContinuity: Capacitor discharged while floating!");
}

// Regression: build() used to overwrite stateAffine/outAffine with the
// default reduction, and computeLiveCoefficients() then reduced from them
// again, applying the live-port correction twice (a constant offset in every
// live-port model, wrong state updates with capacitors). A live pot must
// behave exactly like a fixed resistor of the same value, including state.
void testLivePortMatchesFixedEquivalent() {
    auto buildModel = [](bool live, double fixedOhms) {
        circuit_sim::Circuit c;
        int in = c.addNode();
        int out = c.addNode();
        c.addVoltageSource("V1", in, 0, {circuit_sim::Waveform::Kind::Dc, 0.0});
        if (live)
            c.addVariableResistor("VR1", in, out, 10000.0, "vr_pos", false); // R = pos * 10k
        else
            c.addResistor("R1", in, out, fixedOhms);
        c.addCapacitor("C1", out, 0, 1e-6);
        c.addResistor("RL", out, 0, 20000.0);
        audio_dsp::Config config;
        config.audioInputElement = 0;
        config.audioOutputNode = out;
        config.sampleRate = 48000.0;
        return audio_dsp::build(c, config);
    };
    auto live = buildModel(true, 0.0);
    check(live.ok, "LiveVsFixed: live model build failed");
    if (!live.ok) return;

    // Live coefficients at the default (pos 0.5) equal the model's own default arrays.
    std::unordered_map<std::string, double> none;
    auto defaults = live.computeLiveCoefficients(none);
    std::vector<double> stored;
    stored.insert(stored.end(), live.portAffine.begin(), live.portAffine.end());
    stored.insert(stored.end(), live.portK.begin(), live.portK.end());
    stored.insert(stored.end(), live.stateAffine.begin(), live.stateAffine.end());
    stored.insert(stored.end(), live.stateQ.begin(), live.stateQ.end());
    stored.insert(stored.end(), live.outAffine.begin(), live.outAffine.end());
    stored.insert(stored.end(), live.outQ.begin(), live.outQ.end());
    bool same = defaults.size() == stored.size();
    for (size_t i = 0; same && i < stored.size(); ++i) same = std::abs(defaults[i] - stored[i]) <= 1e-12 * (1.0 + std::abs(stored[i]));
    check(same, "LiveVsFixed: default live coefficients equal the default model (no double application)");

    for (double pos : {0.1, 0.5, 0.9}) {
        auto fixed = buildModel(false, 10000.0 * pos);
        std::unordered_map<std::string, double> params = {{"vr_pos", pos}};
        auto coeffs = live.computeLiveCoefficients(params);
        std::vector<double> wsLive(live.workspaceSize, 0.0), wsFixed(fixed.workspaceSize, 0.0);
        for (int n = 0; n < 200; ++n) {
            const double input = n < 100 ? 1.0 : -0.5; // a step, then a reversal: exercises the capacitor state
            const double a = live.step(wsLive, coeffs.data(), input);
            const double b = fixed.step(wsFixed, nullptr, input);
            if (std::abs(a - b) > 1e-9) {
                checkNear(a, b, 1e-9, "LiveVsFixed: live pot at " + std::to_string(pos) + " differs from a fixed resistor at sample " + std::to_string(n));
                break;
            }
        }
    }
}

int main() {
    std::cout << "Starting AudioDspTests..." << std::endl;
    testResistiveDivider(); std::cout << "testResistiveDivider finished." << std::endl;
    testRCTransient(); std::cout << "testRCTransient finished." << std::endl;
    testNonlinearDiode(); std::cout << "testNonlinearDiode finished." << std::endl;
    testMultiPortNonlinear(); std::cout << "testMultiPortNonlinear finished." << std::endl;
    testCoupledNonlinear(); std::cout << "testCoupledNonlinear finished." << std::endl;
    testPivotingAndSingularity(); std::cout << "testPivotingAndSingularity finished." << std::endl;
    testFailurePaths(); std::cout << "testFailurePaths finished." << std::endl;
    testCoupledLivePorts(); std::cout << "testCoupledLivePorts finished." << std::endl;
    testContinuousSweep(); std::cout << "testContinuousSweep finished." << std::endl;
    testStateContinuity(); std::cout << "testStateContinuity finished." << std::endl;
    testLivePortMatchesFixedEquivalent(); std::cout << "testLivePortMatchesFixedEquivalent finished." << std::endl;

    if (failures == 0) {
        std::cout << "All AudioDspTests passed." << std::endl;
        return 0;
    } else {
        std::cerr << failures << " tests failed." << std::endl;
        return 1;
    }
}

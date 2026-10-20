#include "AudioTestRender.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace audio_test
{

namespace
{
bool isIndependentSource(const circuit_sim::Element& e)
{
    return e.type == circuit_sim::Element::Type::VoltageSource
        || e.type == circuit_sim::Element::Type::CurrentSource;
}

void finish(Trace& t, double quiescent)
{
    t.quiescent = quiescent;
    if (t.volts.empty())
        return;
    double sum = 0.0;
    for (auto v : t.volts) sum += (double)v;
    t.mean = sum / (double)t.volts.size();
    double sq = 0.0, pk = 0.0;
    for (auto v : t.volts)
    {
        const auto d = (double)v - t.mean;
        sq += d * d;
        pk = std::max(pk, std::abs(d));
    }
    t.rms = std::sqrt(sq / (double)t.volts.size());
    t.peak = pk;
}
} // namespace

std::string checkVirtualInput(const circuit_sim::Circuit& circuit, circuit_sim::Node node)
{
    if (node <= 0)
        return "A test input cannot be applied to ground.";
    if (node >= circuit.nodeCount())
        return "That terminal is not part of the simulated circuit.";
    for (const auto& e : circuit.elements())
        if (e.type == circuit_sim::Element::Type::VoltageSource && e.nodes.size() >= 2
            && ((e.nodes[0] == node && e.nodes[1] == 0) || (e.nodes[1] == node && e.nodes[0] == 0)))
            return "That terminal is already driven by source " + e.name
                 + ". Select that source as the test input to replace its excitation instead.";
    return {};
}

Result render(Request request, const std::atomic<bool>* cancel, const std::function<void(double)>& progress)
{
    Result r;
    r.sampleRate = request.sampleRate;
    auto fail = [&](const std::string& message) { r.ok = false; r.error = message; return r; };

    if (request.samples.size() < 2)
        return fail("The test input has no audio samples.");
    if (!(request.sampleRate > 0.0))
        return fail("The test input has no valid sample rate.");
    auto& circuit = request.circuit;
    if (request.outputNode <= 0 || request.outputNode >= circuit.nodeCount())
        return fail("Select an output measurement point (a node other than ground).");
    for (const auto& p : request.probes)
        if (p.node < 0 || p.node >= circuit.nodeCount())
            return fail("Probe " + p.label + " is not part of the simulated circuit.");

    int element = request.inputElement;
    if (element >= 0)
    {
        if (element >= (int)circuit.elements().size() || !isIndependentSource(circuit.elements()[(size_t)element]))
            return fail("The selected test input is not an independent source.");
        r.inputDescription = "replaces the excitation of " + circuit.elements()[(size_t)element].name;
    }
    else
    {
        if (const auto conflict = checkVirtualInput(circuit, request.virtualInputNode); !conflict.empty())
            return fail(conflict);
        circuit_sim::Waveform w;
        element = circuit.addVoltageSource("TEST_IN", request.virtualInputNode, 0, w);
        r.inputDescription = "virtual source at node " + std::to_string(request.virtualInputNode);
    }

    int triodes = 0;
    for (const auto& e : circuit.elements())
        if (e.type == circuit_sim::Element::Type::Triode)
            ++triodes;
    if (triodes > 0)
        r.solverDevices = std::to_string(triodes) + (triodes == 1 ? " triode" : " triodes");

    // Scale the recording to the requested peak (guitar-pickup level, say).
    double peak = 0.0, sq = 0.0;
    for (auto s : request.samples)
    {
        peak = std::max(peak, std::abs((double)s));
        sq += (double)s * (double)s;
    }
    r.inputScale = (request.inputPeakVolts > 0.0 && peak > 0.0) ? request.inputPeakVolts / peak : 1.0;
    const auto n = request.samples.size();
    r.input.resize(n);
    for (size_t i = 0; i < n; ++i)
        r.input[i] = (float)((double)request.samples[i] * r.inputScale);
    r.inputPeak = peak * r.inputScale;
    r.inputRms = std::sqrt(sq / (double)n) * r.inputScale;

    const auto wall0 = std::chrono::steady_clock::now();
    const double dt = 1.0 / request.sampleRate;

    circuit_sim::TransientStepper stepper(circuit);
    if (!stepper.setSourceValue(element, (double)r.input[0]))
        return fail("The solver would not accept the test input: " + stepper.error());
    if (!stepper.init())
        return fail("The circuit has no operating point: " + stepper.error());

    r.output.label = "output";
    r.output.node = request.outputNode;
    r.output.volts.reserve(n);
    r.probes.resize(request.probes.size());
    for (size_t k = 0; k < request.probes.size(); ++k)
    {
        r.probes[k].label = request.probes[k].label;
        r.probes[k].node = request.probes[k].node;
        r.probes[k].volts.reserve(n);
    }

    const double outQuiescent = stepper.voltage(request.outputNode);
    std::vector<double> probeQuiescent(request.probes.size());
    for (size_t k = 0; k < request.probes.size(); ++k)
        probeQuiescent[k] = stepper.voltage(request.probes[k].node);

    auto record = [&] {
        r.output.volts.push_back((float)stepper.voltage(request.outputNode));
        for (size_t k = 0; k < request.probes.size(); ++k)
            r.probes[k].volts.push_back((float)stepper.voltage(request.probes[k].node));
    };
    record();

    for (size_t i = 0; i + 1 < n; ++i)
    {
        if ((i & 255u) == 0)
        {
            if (cancel != nullptr && cancel->load())
                return fail("The test run was cancelled.");
            if (progress && (i & 4095u) == 0)
                progress((double)i / (double)n);
        }
        const double t0 = (double)i * dt;
        const double t1 = (double)(i + 1) * dt;
        stepper.setSourceRamp(element, t0, (double)r.input[i], t1, (double)r.input[i + 1]);
        if (!stepper.step(t1))
            return fail("The solver failed at t = " + std::to_string(t1) + " s (sample " + std::to_string(i + 1)
                        + "): " + stepper.error());
        const double out = stepper.voltage(request.outputNode);
        if (!std::isfinite(out))
            return fail("The solver produced a non-finite output at sample " + std::to_string(i + 1) + ".");
        record();
        ++r.steps;
    }

    finish(r.output, outQuiescent);
    for (size_t k = 0; k < r.probes.size(); ++k)
        finish(r.probes[k], probeQuiescent[k]);
    r.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    if (progress)
        progress(1.0);
    r.ok = true;
    return r;
}

} // namespace audio_test

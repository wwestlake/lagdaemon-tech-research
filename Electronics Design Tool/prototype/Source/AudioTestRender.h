#pragma once

// Offline audio test render (Test Mode).
//
// Pushes a recorded signal through a circuit with the existing internal solver
// (circuit_sim::TransientStepper) and records voltages at chosen nodes. This is
// not a second simulation engine: it drives the same transient step code the
// Transient analysis uses, one audio sample per step, with the stimulus supplied
// as a piecewise-linear source (setSourceRamp). Every nonlinear model the solver
// has (triodes, diodes, BJTs...) therefore works unchanged.
//
// The render works on a private copy of the circuit it is given. A test never
// edits the schematic: an existing source's excitation is replaced inside the
// copy, or a virtual source is added to the copy.

#include "CircuitSolver.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace audio_test
{

struct Probe
{
    std::string label;
    circuit_sim::Node node = 0;
};

struct Request
{
    circuit_sim::Circuit circuit;             // private copy; modified only by this render
    int inputElement = -1;                    // existing independent source to replace, or -1
    circuit_sim::Node virtualInputNode = -1;  // inputElement < 0: a virtual source from this node to ground
    circuit_sim::Node outputNode = 0;
    std::vector<Probe> probes;                // extra voltage probes (quiescent value + trace)
    std::vector<float> samples;               // mono stimulus
    double sampleRate = 48000.0;
    double inputPeakVolts = 0.1;              // stimulus is scaled so its peak is this (0: use as is)
};

struct Trace
{
    std::string label;
    circuit_sim::Node node = 0;
    double quiescent = 0.0;   // voltage at the operating point before the signal
    double mean = 0.0;
    double peak = 0.0;        // max |v - mean|
    double rms = 0.0;         // of v - mean
    std::vector<float> volts; // one per input sample
};

struct Result
{
    bool ok = false;
    std::string error;
    std::string inputDescription;
    double sampleRate = 0.0;
    double inputScale = 1.0;      // volts per unit of the original sample
    double inputPeak = 0.0;       // volts, after scaling
    double inputRms = 0.0;
    std::vector<float> input;     // the stimulus actually applied, volts
    Trace output;
    std::vector<Trace> probes;
    long long steps = 0;
    double wallSeconds = 0.0;
    std::string solverDevices;    // e.g. "1 triode"
};

// "" when a virtual source may be placed at `node`; otherwise why not (the node is
// ground, or an existing voltage source already holds it).
std::string checkVirtualInput(const circuit_sim::Circuit& circuit, circuit_sim::Node node);

// `cancel` (optional) stops the render early; `progress` receives 0..1.
Result render(Request request, const std::atomic<bool>* cancel = nullptr,
              const std::function<void(double)>& progress = {});

} // namespace audio_test

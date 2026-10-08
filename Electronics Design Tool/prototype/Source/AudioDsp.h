#pragma once

// Real-time audio model of a schematic circuit.
//
// The circuit is discretised at the audio sample rate with the same trapezoidal
// companion models CircuitSolver uses for its transient analysis, so a sample
// of audio output matches what a transient run of the same schematic gives.
//
// Per sample the model is reduced to:
//
//   1. P0   = affine(u, S)            ports' voltages with the nonlinear devices removed
//   2. p    = P0 + K * g(p)           Newton solve, only as large as the number of
//                                     nonlinear device ports (0 for a linear circuit)
//   3. S'   = affine(u, S, g(p))      reactive-element history, written back
//   4. y    = affine(u, S, g(p))      output node voltage
//
// With no nonlinear devices steps 2 is empty and this is exactly the linear
// state-space form x[n+1] = A x[n] + B u[n], y = C x[n] + D u[n], found by
// inverting the (constant) circuit matrix once. With diodes, transistors or
// rail-limited op amps only the small port system is solved each sample (the
// DK method), warm-started from the previous sample.
//
// The same data drives a C++ reference evaluator (Model::step), used by the
// tests to check the algorithm against the transient solver, and the Frust
// generator, whose output the tests check against the reference evaluator.

#include "CircuitSolver.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace audio_dsp
{
struct Config
{
    double sampleRate = 48000.0;
    int audioInputElement = -1;        // voltage-source element fed with the audio input
    int audioOutputNode = -1;          // node whose voltage is the audio output
    double inputVolts = 1.0;           // volts for a full-scale (1.0) input sample
    double outputFullScaleVolts = 1.0; // volts that map to a full-scale (1.0) output sample
};

// An independent source that is not constant; the host writes its value into the
// workspace slot before every sample.
struct TimedSource
{
    int element = -1;
    int slot = 0;
    circuit_sim::Waveform wave;
};

// One nonlinear device and the ports (voltage functionals) it is solved in.
struct Device
{
    enum class Kind { Diode, Bjt, Mos, OpAmp };
    Kind kind = Kind::Diode;
    int element = -1;
    double sign = 1.0;       // +1 NPN/NMOS, -1 PNP/PMOS
    int port = 0;            // first port index
    int ports = 1;
    circuit_sim::Element parameters; // the element, for its model
    bool railPlusPort = false, railMinusPort = false; // op amp: rails are ports
};

// A live parameter port extracted from the linear circuit
struct LivePort
{
    enum class Kind { Switch, VariableResistor };
    Kind kind = Kind::VariableResistor;
    int element = -1;
    int port = 0; // The port index within the LIVE ports block
    std::string paramId; // e.g., "SW1_state", "POT1_wiper"
    double baseResistance = 1000.0; // For potentiometers, total resistance
    bool isWiperToPin2 = false; // For POTs
};

class Model
{
public:
    bool ok = false;
    std::string error;
    std::vector<std::string> notes; // things the user should know (not errors)

    int unknowns = 0;       // size of the MNA system
    int stateCount = 0;     // capacitors + inductors
    int inputCount = 1;     // audio + timed sources
    int portCount = 0;      // nonlinear ports (0: linear circuit)
    int livePortCount = 0;  // dynamic linear ports (potentiometers, switches)
    bool isNonlinear() const { return portCount > 0; }

    // Workspace (a flat array of doubles the generated code reads and writes):
    //   [0, stateCount)                   reactive history
    //   [timedBase, +timedSources.size()) timed source values, written by the host
    //   [portBase, +portCount)            last solved ports (warm start)
    //   [diagBase]                        samples whose Newton solve did not converge
    //   [scratchBase, ...)                Newton scratch
    int workspaceSize = 0;
    int timedBase = 0, portBase = 0, diagBase = 0, scratchBase = 0;
    std::vector<double> initialWorkspace; // reactive history and ports at the DC operating point
    std::vector<TimedSource> timedSources;
    std::vector<LivePort> livePorts;

    double sampleRate = 48000.0;
    double inputVolts = 1.0;
    double outputFullScaleVolts = 1.0;
    double outputOffsetVolts = 0.0; // DC level of the output node at rest, removed from the output

    // Reference evaluator: one sample, returns the output in volts with the DC
    // level removed. `ws` is a workspace laid out as above.
    double step(std::vector<double>& ws, const double* coeffs, double audioInVolts) const;

    // Frust source defining
    //   pub fn process_sample(audio_in: f64, ws: Array<f64, workspaceSize>, coeffs: Array<f64, coeffSize>) -> f64
    // where audio_in is the input in volts and the result is the output in volts
    // with its DC level removed. Compile it with a manifest declaring the host
    // functions in requiredHostFunctions().
    std::string frustSource() const;
    static std::vector<std::string> requiredHostFunctions();

    std::string describe() const; // one line for the log

    // ---- data (public so the generator and tests can read it) --------------
    std::vector<Device> devices;
    
    // The reduced coefficients currently in use (used by `step` if `coeffs` is null)
    std::vector<double> portAffine;  // portCount x (1 + inputCount + stateCount)
    std::vector<double> portK;       // portCount x portCount
    std::vector<double> stateAffine; // stateCount x (1 + inputCount + stateCount)
    std::vector<double> stateQ;      // stateCount x portCount
    std::vector<double> outAffine;   // 1 x (1 + inputCount + stateCount)
    std::vector<double> outQ;        // 1 x portCount
    
    // The BASE matrices (including live ports)
    // Ordered with Live Ports first, then Nonlinear Ports
    std::vector<double> baseAffine;  // (livePortCount + portCount) x affineCols
    std::vector<double> baseK;       // (livePortCount + portCount) x (livePortCount + portCount)
    std::vector<double> baseStateQ;  // stateCount x (livePortCount + portCount)
    std::vector<double> baseOutQ;    // 1 x (livePortCount + portCount)
    
    int coeffSize() const;
    
    // Generates a reduced coefficient array given a set of UI parameters
    std::vector<double> computeLiveCoefficients(const std::unordered_map<std::string, double>& liveParams) const;

    // Newton settings.
    int maxIterations = 40;
    double tolerance = 1e-9;      // volts
    double junctionStepLimit = 0.3; // volts per iteration on exponential junctions
    std::vector<bool> junctionPort; // per port: limited by junctionStepLimit
};

// Builds the model, or sets ok = false with an explanation (an unsupported
// element, a singular circuit, a missing input or output).
Model build(const circuit_sim::Circuit& circuit, const Config& config);

// Number written into Frust source: plain decimal digits (Frust has no
// exponent notation), parenthesised when negative.
std::string frustNumber(double value);
}

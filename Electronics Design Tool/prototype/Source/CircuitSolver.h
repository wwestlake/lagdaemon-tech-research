#pragma once

// General circuit solver (modified nodal analysis), framework-agnostic.
//
// Builds a circuit from nodes and elements, then runs the SPICE analysis set:
//   - DC operating point (Newton-Raphson with step limiting, source stepping)
//   - DC sweep of any source or element value, optionally nested
//   - transient (trapezoidal integration, Newton at every step)
//   - AC small-signal sweep (linearised at the operating point)
//   - noise (thermal, shot and channel noise referred to an output and input)
//   - small-signal DC transfer function (gain, input and output resistance)
//   - sensitivity of an output to every element value (DC and AC)
//   - poles and zeros of a transfer function
//   - Fourier analysis / THD of a waveform
// plus temperature scaling of every model (atTemperature).
//
// Node 0 is ground. Elements: R, C, L (with optional coupling for
// transformers), independent V/I sources (DC, sine, square, pulse, PWL,
// exponential), controlled sources E/G/H/F, diodes (incl. zener breakdown),
// BJTs (NPN/PNP, Ebers-Moll with Early effect), MOSFETs (NMOS/PMOS, square
// law) and a rail-limited op amp. The host application maps its schematic
// onto this; this file knows nothing about schematics or UI.

#include <complex>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace circuit_sim
{
using Node = int;

constexpr double nominalTemperatureC = 27.0;

struct Waveform
{
    enum class Kind { Dc, Sine, Square, Pulse, Pwl, Exp };
    Kind kind = Kind::Dc;
    double offset = 0.0;      // DC value, the offset of a periodic waveform, or V1 of pulse/exp
    double amplitude = 0.0;   // peak (sine/square)
    double frequency = 0.0;   // Hz (sine/square)
    double phaseDegrees = 0.0;
    double duty = 0.5;        // square
    // Pulse: offset -> pulsed after delay, rise, width, fall; repeats every period (0 = once).
    double pulsed = 0.0, delay = 0.0, rise = 0.0, fall = 0.0, width = 0.0, period = 0.0;
    // Exponential: offset -> pulsed starting at delay (tau1), back to offset from delay2 (tau2).
    double delay2 = 0.0, tau1 = 0.0, tau2 = 0.0;
    std::vector<std::pair<double, double>> points; // PWL (time, value), times ascending
    double acMagnitude = 0.0; // small-signal magnitude for AC analysis
    double acPhaseDegrees = 0.0;

    double valueAt(double t) const;
    double dcValue() const;   // value used for the operating point (t = 0 for pulse/PWL/exp)
    // Times where the waveform has corners, inside [0, stop]; transient steps land on them.
    std::vector<double> breakpoints(double stop) const;
};

struct DiodeModel
{
    double saturationCurrent = 1e-14;
    double emission = 1.0;
    double breakdownVoltage = 0.0; // > 0 enables reverse breakdown (zener)
    double transitTime = 0.0;      // tt: diffusion capacitance tt * gd in small-signal analyses
    double vt = 0.025852;          // thermal voltage, set by atTemperature
};

struct BjtModel
{
    double saturationCurrent = 1e-14;
    double betaForward = 100.0;
    double betaReverse = 1.0;
    double earlyVoltage = 0.0;     // VAF, 0 = no Early effect
    double transitTime = 0.0;      // tf: diffusion capacitance tf * gm (sets fT with the junction caps)
    double vt = 0.025852;
};

struct MosModel
{
    double threshold = 1.0;        // magnitude, volts
    double transconductance = 2e-3; // K (A/V^2)
    double lambda = 0.01;
};

struct JfetModel
{
    double pinchoff = 2.0;         // magnitude |Vp|, volts
    double idss = 10e-3;           // A
    double lambda = 0.0;           // Channel-length modulation
    double is = 1e-14;             // Gate junction saturation current
    double n = 1.0;                // Gate junction emission coefficient
};

struct OpAmpModel
{
    double gain = 1e5;
    double railDrop = 1.5;         // output stays this far inside the rails
    bool limited = true;           // false: a linear gain stage (the rails are applied by a later stage)
};

struct Element
{
    enum class Type
    {
        Resistor, Capacitor, Inductor, Coupling,
        VoltageSource, CurrentSource,
        BehavioralVoltageSource, BehavioralCurrentSource,
        Vcvs, Vccs, Ccvs, Cccs,
        Diode, Npn, Pnp, Nmos, Pmos, Njfet, Pjfet, OpAmp,
        VariableResistor, Switch, VoltageControlledSwitch, CurrentControlledSwitch
    };

    Type type = Type::Resistor;
    std::string name;
    std::vector<Node> nodes; // see the add* helpers for terminal order
    std::string modelName;   // selected SPICE .MODEL/.SUBCKT name, when bound
    double value = 0.0;      // R ohms, C farads, L henries, gain, coupling k
    double offResistance = 1e9; // controlled switch Roff
    double threshold = 0.0;  // controlled switch Vt/It
    double hysteresis = 0.0; // controlled switch Vh/Ih
    bool hasInitialCondition = false; // C: volts across node[0]-node[1], L: current node[0]->node[1]
    double initialCondition = 0.0;
    std::string expression;  // Xyce behavioral source expression
    double tc1 = 0.0, tc2 = 0.0; // resistor temperature coefficients (1/K, 1/K^2)
    bool noiseless = false;  // e.g. instrument input resistances
    Waveform wave;           // sources
    DiodeModel diode;
    BjtModel bjt;
    MosModel mos;
    JfetModel jfet;
    OpAmpModel opamp;
    int control = -1;        // Ccvs/Cccs: index of the controlling voltage source; Coupling: first inductor
    int control2 = -1;       // Coupling: second inductor
    std::string paramId;     // For live parameters
    bool isWiperToPin2 = false; // For potentiometers
};

class Circuit
{
public:
    Node addNode();
    int nodeCount() const { return nodes; }

    int addResistor(const std::string& name, Node a, Node b, double ohms);
    int addVariableResistor(const std::string& name, Node a, Node b, double totalResistance, const std::string& paramId, bool isWiperToPin2);
    int addSwitch(const std::string& name, Node a, Node b, const std::string& paramId);
    int addVoltageControlledSwitch(const std::string& name, Node a, Node b, Node controlPlus, Node controlMinus,
                                   double ron, double roff, double threshold, double hysteresis);
    int addCurrentControlledSwitch(const std::string& name, Node a, Node b, int controllingSource,
                                   double ron, double roff, double threshold, double hysteresis);
    int addCapacitor(const std::string& name, Node a, Node b, double farads);
    int addInductor(const std::string& name, Node a, Node b, double henries);
    int addCoupling(const std::string& name, int inductorA, int inductorB, double k);
    // Current through a voltage source flows from + through the source to -.
    int addVoltageSource(const std::string& name, Node plus, Node minus, Waveform wave);
    // Current flows from `from` through the source into `to`.
    int addCurrentSource(const std::string& name, Node from, Node to, Waveform wave);
    int addBehavioralVoltageSource(const std::string& name, Node plus, Node minus, std::string expression);
    int addBehavioralCurrentSource(const std::string& name, Node from, Node to, std::string expression);
    int addVcvs(const std::string& name, Node outPlus, Node outMinus, Node ctrlPlus, Node ctrlMinus, double gain);
    int addVccs(const std::string& name, Node outFrom, Node outTo, Node ctrlPlus, Node ctrlMinus, double gain);
    int addCcvs(const std::string& name, Node outPlus, Node outMinus, int controllingSource, double gain);
    int addCccs(const std::string& name, Node outFrom, Node outTo, int controllingSource, double gain);
    int addDiode(const std::string& name, Node anode, Node cathode, DiodeModel model = {});
    int addBjt(const std::string& name, bool npn, Node collector, Node base, Node emitter, BjtModel model = {});
    int addMosfet(const std::string& name, bool nChannel, Node drain, Node gate, Node source, MosModel model = {});
    int addJfet(const std::string& name, bool nChannel, Node drain, Node gate, Node source, JfetModel model = {});
    int addOpAmp(const std::string& name, Node inPlus, Node inMinus, Node out, Node railPlus, Node railMinus, OpAmpModel model = {});

    const std::vector<Element>& elements() const { return parts; }
    std::vector<Element>& elements() { return parts; }
    int find(const std::string& name) const; // element index or -1
    void setNodeInitialVoltage(Node node, double volts);
    const std::map<Node, double>& nodeInitialVoltages() const { return initialNodeVoltages; }

private:
    int add(Element e);
    int nodes = 1; // ground
    std::vector<Element> parts;
    std::map<Node, double> initialNodeVoltages;
};

struct Options
{
    double gmin = 1e-12;
    double absTol = 1e-9;
    double relTol = 1e-6;
    int maxIterations = 200;
    double maxStepVolts = 0.5; // Newton step limit per node per iteration
    double temperatureC = nominalTemperatureC; // for noise (kT); models are scaled with atTemperature
};

// ---- element parameters (for sweeps, steps, sensitivity, tolerances) ----------

// Names of the numeric parameters an element type has, first is the primary
// one ("value" for R/C/L/gains, "dc" for sources, "is" for diodes, "beta" for BJTs...).
std::vector<std::string> parameterNames(Element::Type type);
bool getParameter(const Element& e, const std::string& name, double& out);
bool setParameter(Element& e, const std::string& name, double value);

// A copy of the circuit with every model at `celsius`: resistors by tc1/tc2,
// diode and BJT saturation currents and thermal voltages, MOSFET threshold
// (-2 mV/K) and K (T^-1.5). Models are given at 27 C.
Circuit atTemperature(const Circuit& circuit, double celsius);

// ---- results --------------------------------------------------------------------

struct OperatingPoint
{
    bool ok = false;
    std::string error;
    std::vector<double> voltages;      // per node, [0] = 0
    std::vector<double> sourceCurrents; // per element (voltage sources, inductors, E/H, op amps), 0 otherwise
    int iterations = 0;
};

// Current into each terminal of an element at an operating point (or any
// solved state with the same voltages/branch currents), in terminal order.
std::vector<double> terminalCurrents(const Circuit& circuit, const OperatingPoint& op, int element);
// Power absorbed by an element (negative: delivering).
double absorbedPower(const Circuit& circuit, const OperatingPoint& op, int element);

struct DeviceInfo
{
    std::string region;                                   // "forward active", "saturation", "triode"...
    std::vector<std::pair<std::string, double>> values;   // name -> SI value, e.g. {"Ic", 1e-3}
    std::vector<std::string> units;                       // parallel to values
};
// Operating-point details of a diode/BJT/MOSFET/op amp (empty for other elements).
DeviceInfo deviceInfo(const Circuit& circuit, const OperatingPoint& op, int element);

struct TransientResult
{
    bool ok = false;
    std::string error;
    std::vector<double> time;
    std::vector<std::vector<double>> voltages;       // [sample][node]
    std::vector<std::vector<double>> sourceCurrents; // [sample][element]
};

struct TransientSettings
{
    double stop = 1e-3;
    double step = 1e-6;        // fixed step; breakpoints of the sources are also hit exactly
    double start = 0.0;        // samples before this are not stored
    int maxSamples = 4000;     // thins the stored waveform; every step is still solved
};

struct AcResult
{
    bool ok = false;
    std::string error;
    std::vector<double> frequency;
    std::vector<std::vector<std::complex<double>>> voltages; // [point][node]
    std::vector<std::vector<std::complex<double>>> branchCurrents; // [point][element] (branch elements), 0 otherwise
};

struct SweepAxis
{
    int element = -1;
    std::string parameter = "dc"; // see parameterNames
    std::vector<double> values;
    bool temperature = false;     // sweep temperature (C) instead of an element
};

struct DcSweepResult
{
    bool ok = false;
    std::string error;
    std::vector<double> inner;                       // inner sweep values
    std::vector<double> outer;                       // outer values ({0} when there is no outer sweep)
    std::vector<std::vector<OperatingPoint>> points; // [outer][inner]
};

struct NoiseContribution
{
    std::string element;
    std::string source;         // "thermal", "shot Ic", "shot Ib", "channel"
    double integratedOutputV2 = 0.0; // V^2 at the output over the band
};

struct NoiseResult
{
    bool ok = false;
    std::string error;
    std::vector<double> frequency;
    std::vector<double> outputDensity;     // V/sqrt(Hz)
    std::vector<double> inputDensity;      // V/sqrt(Hz) or A/sqrt(Hz) (current-source input)
    std::vector<double> gain;              // |output / input|
    double integratedOutputRms = 0.0;      // V rms over the band
    double integratedInputRms = 0.0;
    std::vector<NoiseContribution> contributions; // sorted, largest first
    std::vector<std::string> noiselessElements;   // elements modelled without noise (ideal op amps...)
};

struct TransferFunctionResult
{
    bool ok = false;
    std::string error;
    double gain = 0.0;             // dVout / dInput (V/V, or V/A for a current-source input)
    double inputResistance = 0.0;  // seen by the input source
    double outputResistance = 0.0; // at the output node pair
};

struct SensitivityItem
{
    std::string element;
    std::string parameter;
    double value = 0.0;
    double absolute = 0.0;   // d(output) / d(parameter)
    double normalized = 0.0; // d(output) / (d(parameter)/parameter) / 100: output change per 1 % change
};

struct SensitivityResult
{
    bool ok = false;
    std::string error;
    double output = 0.0; // nominal output (V for DC, dB for AC)
    std::vector<SensitivityItem> items; // sorted by |normalized|, largest first
};

struct PoleZeroResult
{
    bool ok = false;
    std::string error;
    std::vector<std::complex<double>> poles; // rad/s
    std::vector<std::complex<double>> zeros; // rad/s
    double dcGain = 0.0;
    int cancelled = 0; // coincident pole-zero pairs removed (modes outside this transfer function)
};

struct FourierResult
{
    bool ok = false;
    std::string error;
    double fundamental = 0.0;
    double dc = 0.0;
    std::vector<double> magnitude; // [h] peak amplitude, h = 1..harmonics
    std::vector<double> phaseDegrees;
    double thdPercent = 0.0;
};

// ---- analyses -------------------------------------------------------------------

OperatingPoint solveOperatingPoint(const Circuit& circuit, const Options& options = {});
// Starts Newton from `guess` (voltages per node + sourceCurrents per element), falling back to
// a cold start; used by sweeps to follow a solution.
OperatingPoint solveOperatingPointFrom(const Circuit& circuit, const OperatingPoint& guess, const Options& options = {});

// `maxSamples` thins the stored waveform; every step is still solved.
TransientResult solveTransient(const Circuit& circuit, double stopTime, double timeStep,
                               const Options& options = {}, int maxSamples = 4000);
TransientResult solveTransient(const Circuit& circuit, const TransientSettings& settings, const Options& options = {});

// Source AC magnitudes/phases are taken from each source's waveform.
AcResult solveAc(const Circuit& circuit, double startHz, double stopHz, int pointsPerDecade, const Options& options = {});
// Frequencies given explicitly (linear or any spacing).
AcResult solveAcAt(const Circuit& circuit, const std::vector<double>& frequencies, const Options& options = {});

// Up to two nested sweeps: `inner` varies fastest; `outer.element < 0 && !outer.temperature` = none.
DcSweepResult solveDcSweep(const Circuit& circuit, const SweepAxis& inner, const SweepAxis& outer, const Options& options = {});

// Output = V(outPlus) - V(outMinus); input = independent source element (its AC is set to 1).
NoiseResult solveNoise(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource,
                       double startHz, double stopHz, int pointsPerDecade, const Options& options = {});

TransferFunctionResult solveTransferFunction(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource,
                                             const Options& options = {});

// DC: sensitivity of V(outPlus) - V(outMinus) to every element parameter.
SensitivityResult solveDcSensitivity(const Circuit& circuit, Node outPlus, Node outMinus, const Options& options = {});
// AC: sensitivity of the gain in dB from `inputSource` to the output at `frequency`.
SensitivityResult solveAcSensitivity(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource,
                                     double frequency, const Options& options = {});

PoleZeroResult solvePoleZero(const Circuit& circuit, Node outPlus, Node outMinus, int inputSource, const Options& options = {});

// Fourier series of `values(time)` over the last `periods` periods of `fundamentalHz`.
FourierResult fourier(const std::vector<double>& time, const std::vector<double>& values, double fundamentalHz,
                      int harmonics = 9, int periods = 1);

// Parses engineering values: 4.7k, 10u, 2.2n, 100p, 1meg, 3M (mega), 5m (milli),
// optional trailing unit letters (4.7kohm, 10uF, 12V). Returns false if unparseable.
bool parseValue(const std::string& text, double& out);
// SPICE reads a capital M as milli; the default here reads it as mega.
void setCapitalMIsMilli(bool milli);
std::string formatValue(double value, const std::string& unit, int significant = 3);
}

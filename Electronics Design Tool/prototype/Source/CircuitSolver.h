#pragma once

// General circuit solver (modified nodal analysis), framework-agnostic.
//
// Builds a circuit from nodes and elements, then runs:
//   - DC operating point (Newton-Raphson with step limiting, source stepping)
//   - transient (trapezoidal integration, Newton at every step)
//   - AC small-signal sweep (linearised at the operating point)
//
// Node 0 is ground. Elements: R, C, L (with optional coupling for
// transformers), independent V/I sources with DC/sine/square waveforms,
// controlled sources E/G/H/F, diodes (incl. zener breakdown), BJTs (NPN/PNP,
// Ebers-Moll), MOSFETs (NMOS/PMOS, square law) and a rail-limited op amp.
// The host application maps its schematic onto this; this file knows
// nothing about schematics or UI.

#include <complex>
#include <string>
#include <vector>

namespace circuit_sim
{
using Node = int;

struct Waveform
{
    enum class Kind { Dc, Sine, Square };
    Kind kind = Kind::Dc;
    double offset = 0.0;      // DC value, or the offset of a periodic waveform
    double amplitude = 0.0;   // peak
    double frequency = 0.0;   // Hz
    double phaseDegrees = 0.0;
    double duty = 0.5;        // square
    double acMagnitude = 0.0; // small-signal magnitude for AC analysis

    double valueAt(double t) const;
    double dcValue() const { return kind == Kind::Dc ? offset : offset; }
};

struct DiodeModel
{
    double saturationCurrent = 1e-14;
    double emission = 1.0;
    double breakdownVoltage = 0.0; // > 0 enables reverse breakdown (zener)
};

struct BjtModel
{
    double saturationCurrent = 1e-14;
    double betaForward = 100.0;
    double betaReverse = 1.0;
};

struct MosModel
{
    double threshold = 1.0;        // magnitude, volts
    double transconductance = 2e-3; // K (A/V^2)
    double lambda = 0.01;
};

struct OpAmpModel
{
    double gain = 1e5;
    double railDrop = 1.5;         // output stays this far inside the rails
};

struct Element
{
    enum class Type
    {
        Resistor, Capacitor, Inductor, Coupling,
        VoltageSource, CurrentSource,
        Vcvs, Vccs, Ccvs, Cccs,
        Diode, Npn, Pnp, Nmos, Pmos, OpAmp
    };

    Type type = Type::Resistor;
    std::string name;
    std::vector<Node> nodes; // see the add* helpers for terminal order
    double value = 0.0;      // R ohms, C farads, L henries, gain, coupling k
    Waveform wave;           // sources
    DiodeModel diode;
    BjtModel bjt;
    MosModel mos;
    OpAmpModel opamp;
    int control = -1;        // Ccvs/Cccs: index of the controlling voltage source; Coupling: first inductor
    int control2 = -1;       // Coupling: second inductor
};

class Circuit
{
public:
    Node addNode();
    int nodeCount() const { return nodes; }

    int addResistor(const std::string& name, Node a, Node b, double ohms);
    int addCapacitor(const std::string& name, Node a, Node b, double farads);
    int addInductor(const std::string& name, Node a, Node b, double henries);
    int addCoupling(const std::string& name, int inductorA, int inductorB, double k);
    // Current through a voltage source flows from + through the source to -.
    int addVoltageSource(const std::string& name, Node plus, Node minus, Waveform wave);
    // Current flows from `from` through the source into `to`.
    int addCurrentSource(const std::string& name, Node from, Node to, Waveform wave);
    int addVcvs(const std::string& name, Node outPlus, Node outMinus, Node ctrlPlus, Node ctrlMinus, double gain);
    int addVccs(const std::string& name, Node outFrom, Node outTo, Node ctrlPlus, Node ctrlMinus, double gain);
    int addCcvs(const std::string& name, Node outPlus, Node outMinus, int controllingSource, double gain);
    int addCccs(const std::string& name, Node outFrom, Node outTo, int controllingSource, double gain);
    int addDiode(const std::string& name, Node anode, Node cathode, DiodeModel model = {});
    int addBjt(const std::string& name, bool npn, Node collector, Node base, Node emitter, BjtModel model = {});
    int addMosfet(const std::string& name, bool nChannel, Node drain, Node gate, Node source, MosModel model = {});
    int addOpAmp(const std::string& name, Node inPlus, Node inMinus, Node out, Node railPlus, Node railMinus, OpAmpModel model = {});

    const std::vector<Element>& elements() const { return parts; }
    std::vector<Element>& elements() { return parts; }

private:
    int add(Element e);
    int nodes = 1; // ground
    std::vector<Element> parts;
};

struct OperatingPoint
{
    bool ok = false;
    std::string error;
    std::vector<double> voltages;      // per node, [0] = 0
    std::vector<double> sourceCurrents; // per element (voltage sources, inductors, E/H, op amps), 0 otherwise
    int iterations = 0;
};

struct TransientResult
{
    bool ok = false;
    std::string error;
    std::vector<double> time;
    std::vector<std::vector<double>> voltages;       // [sample][node]
    std::vector<std::vector<double>> sourceCurrents; // [sample][element]
};

struct AcResult
{
    bool ok = false;
    std::string error;
    std::vector<double> frequency;
    std::vector<std::vector<std::complex<double>>> voltages; // [point][node]
};

struct Options
{
    double gmin = 1e-12;
    double absTol = 1e-9;
    double relTol = 1e-6;
    int maxIterations = 200;
    double maxStepVolts = 0.5; // Newton step limit per node per iteration
};

OperatingPoint solveOperatingPoint(const Circuit& circuit, const Options& options = {});
// `maxSamples` thins the stored waveform; every step is still solved.
TransientResult solveTransient(const Circuit& circuit, double stopTime, double timeStep,
                               const Options& options = {}, int maxSamples = 4000);
AcResult solveAc(const Circuit& circuit, double startHz, double stopHz, int pointsPerDecade, const Options& options = {});

// Parses engineering values: 4.7k, 10u, 2.2n, 100p, 1meg, 3M (mega), 5m (milli),
// optional trailing unit letters (4.7kohm, 10uF, 12V). Returns false if unparseable.
bool parseValue(const std::string& text, double& out);
std::string formatValue(double value, const std::string& unit, int significant = 3);
}

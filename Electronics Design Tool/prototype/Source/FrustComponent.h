#pragma once

// FRust programmable electronic components.
//
// A component definition is a named part whose electrical behaviour is a
// FRust node program (made in the Node Designer). Its external pins have a
// stable identity (their index and id: what the netlist connects), an
// electrical role and a place on the symbol (side and order, the common pin
// layout shared with Sub Diagram blocks, schematic::BlockPort). It has
// numeric parameters (each instance can set its own values) and state
// variables with initial values.
//
// In a simulation each schematic instance is a circuit_sim::ProgrammableDevice
// with its own state. The internal solver calls the compiled program at every
// Newton iteration with that iterate's pin voltages; the program reads pins,
// time, parameters and state and sets each pin's electrical model. State
// written by the program is kept only when the solver accepts the step (the
// device keeps the committed state and a trial copy).
//
// Pin model. Every pin is a Thevenin/Norton branch from the pin to its
// reference (ground, or another pin of the component):
//     current into the circuit at the pin = I + (V - (Vpin - Vref)) / R
// with the same current returning at the reference pin. The program sets
// V (pc_drive), R (pc_set_resistance; none = open) and I (pc_drive_current)
// at any evaluation, from anything it reads; a pin keeps its last accepted
// setting until the program changes it. The role gives the start values:
//     input           V = 0, R = open,             I = 0   (no load)
//     voltage_output  V = 0, R = outputResistance, I = 0   (pc_drive sets V)
//     current_output  V = 0, R = open,             I = 0   (pc_drive sets I)
// So an input with a resistance is an input impedance (it loads the net),
// an output with a resistance is an output impedance, and a pin referenced
// to another pin is a floating branch between the two (a programmable
// resistor, a floating source, a controlled current between terminals).
//
// The program reaches the electrical interface through host functions
// (djehuti_pc_*), which the node compiler's component nodes call. Outside a
// simulation (Compile & Run or the debugger in the Node Designer) they read 0
// and drive nothing.

#include <JuceHeader.h>

#include "CircuitSolver.h"
#include "FrustEngine.h"
#include "SchematicSymbols.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace frust_component
{
enum class PinRole { Input, VoltageOutput, CurrentOutput };
juce::String roleName(PinRole role);                   // input, voltage_output, current_output
bool parseRole(const juce::String& text, PinRole& role);

struct Pin
{
    juce::String id;   // the pin's name; unique in the component
    PinRole role = PinRole::Input;
    schematic::PinSide side = schematic::PinSide::Left;
    int order = -1;
    juce::String reference; // the pin its branch returns through; "" = ground

    Pin() = default;
    Pin(juce::String pinId, PinRole pinRole, juce::String referencePin = {})
        : id(std::move(pinId)), role(pinRole), reference(std::move(referencePin)) {}
};

struct Parameter
{
    juce::String name;
    double defaultValue = 0.0;
};

struct StateVariable
{
    juce::String name;
    double initial = 0.0;
};

struct Definition
{
    juce::String name;
    juce::String description;
    std::vector<Pin> pins;               // index = electrical identity
    std::vector<Parameter> parameters;
    std::vector<StateVariable> state;
    double outputResistance = 1.0;       // ohms, each voltage output
    juce::String programFile;            // the node program, beside the definition (<name>.frnode.json)

    int pinIndex(const juce::String& id) const;
    // The symbol's pins (common pin layout), in pin order.
    std::vector<schematic::BlockPort> ports() const;
    // Takes side and order from a laid-out port list (same pins, same order).
    void setLayout(const std::vector<schematic::BlockPort>& ports);
    // What the node compiler needs to resolve the component nodes' names.
    juce::var compilerContext() const;
};

bool isValidName(const juce::String& name); // letters, digits, _ ; starts with a letter
// Checks pins, parameters and state for empty or duplicate names.
juce::String problemsWith(const Definition& definition);

juce::var toVar(const Definition& definition);
bool fromVar(const juce::var& v, Definition& definition, juce::String& error);
bool load(const juce::File& file, Definition& definition, juce::String& error);
bool save(const juce::File& file, const Definition& definition, juce::String& error);

// The compiled programs, by definition name. A recompile replaces the entry;
// devices keep the code they were made with until they are released.
class Library
{
public:
    static Library& instance();

    struct Compiled;
    // Compiles the generated FRust source (the node compiler's output for the
    // definition's program). `key` identifies the inputs (definition and
    // program); the same key is not compiled twice.
    bool compile(const Definition& definition, const std::string& frustSource, const std::string& key, juce::String& error);
    bool isCompiled(const juce::String& name, const std::string& key) const;
    // A device for one schematic instance: its own state, the definition's
    // parameters with the instance's values (by name; missing ones default).
    std::shared_ptr<circuit_sim::ProgrammableDevice> makeDevice(const Definition& definition,
                                                                const std::map<juce::String, double>& parameterValues,
                                                                const juce::String& instanceName, juce::String& error) const;

private:
    Library();
    frust_engine::Engine engine;
    std::map<juce::String, std::shared_ptr<Compiled>> compiled;
    mutable std::mutex mutex;
    int nextUnit = 1;
};

// The host functions the component nodes call; registered with the FRust
// plugin host once (the Library does it).
const std::vector<std::string>& hostFunctionNames();
void registerHostFunctions();
}

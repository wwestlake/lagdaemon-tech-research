#pragma once

#include "CircuitSolver.h"
#include "Mechanical.h"
#include "FrustComponent.h"
#include "MechanicalSceneModel.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sim_panel
{

// -----------------------------------------------------------------------------
// Subsystem Types & Ports
// -----------------------------------------------------------------------------

enum class PortDirection
{
    Input,
    Output,
    Bidirectional
};

enum class PortDomain
{
    Electrical,     // Volts / Amps node
    Mechanical,     // Shaft speed / torque or angle
    SignalControl,  // 0-5V logical or continuous analog control
    Observable      // Passive instrument input that may watch any domain
};

struct SubsystemPort
{
    std::string name;
    PortDirection direction = PortDirection::Input;
    PortDomain domain = PortDomain::Electrical;
    std::string unit;
    std::string description;

    juce::var toJson() const;
    static SubsystemPort fromJson(const juce::var& json);
};

enum class SubsystemKind
{
    DCMotor,                    // Built-in physical electromechanical motor module
    MotorSpeedController,       // Built-in closed-loop PID controller with interlocks & anti-windup
    CoordinatedSequencer,       // Built-in multi-device startup & interlock coordinator
    SchematicReference,         // User-defined schematic diagram (Sub-diagram / .diagram.json)
    ProgrammableComponent,      // User-defined Node Designer / FRust programmable component (.frcomp.json)
    NestedAssembly,             // Another complete SystemAssembly (.assembly.json)
    PlotPanel                   // Passive N-channel plotting instrument
};

// Definition of a reusable subsystem module
struct SubsystemDefinition
{
    std::string id;             // e.g. "dc_motor_module", "pid_speed_controller", "custom_circuit_block"
    std::string displayName;
    SubsystemKind kind = SubsystemKind::DCMotor;
    std::string description;

    // External ports exposed by this subsystem
    std::vector<SubsystemPort> ports;

    // Default parameters for this subsystem
    std::map<std::string, double> defaultNumericParams;
    std::map<std::string, std::string> defaultTextParams;

    // For User-defined references:
    std::string referenceFile;  // diagram path, frcomp name, or assembly file
    std::string diagramSheet;   // sub-diagram sheet name if applicable
    juce::var schematicData;    // embedded or loaded circuit diagram JSON
    juce::var componentData;    // embedded or loaded FRust programmable component JSON

    const SubsystemPort* findPort(const std::string& portName) const;

    // Factory methods for user-defined subsystems
    static SubsystemDefinition fromSchematicJson(const juce::var& json, const std::string& defId, const std::string& name);
    static bool loadSchematicDefinition(const juce::File& file, SubsystemDefinition& out, juce::String& error);
    static SubsystemDefinition fromFrustComponent(const frust_component::Definition& def, const std::string& defId = "");
    static bool loadFrustComponentDefinition(const juce::File& file, SubsystemDefinition& out, juce::String& error);

    juce::var toJson() const;
    static SubsystemDefinition fromJson(const juce::var& json);
};

// An instance of a subsystem inside an assembly
struct SubsystemInstance
{
    std::string instanceId;         // e.g. "motorA", "motorB", "controllerA", "sequencer"
    std::string definitionId;       // e.g. "dc_motor_module"
    std::string title;              // e.g. "Drive Motor A"
    double canvasX = 120.0;         // Graphical Machine Builder layout; does not affect connectivity
    double canvasY = 120.0;

    // Instance-specific parameter overrides
    std::map<std::string, double> numericParams;
    std::map<std::string, std::string> textParams;

    double getParam(const std::string& name, double fallback = 0.0) const
    {
        auto it = numericParams.find(name);
        return it != numericParams.end() ? it->second : fallback;
    }

    std::string getTextParam(const std::string& name, const std::string& fallback = "") const
    {
        auto it = textParams.find(name);
        return it != textParams.end() ? it->second : fallback;
    }

    juce::var toJson() const;
    static SubsystemInstance fromJson(const juce::var& json);
};

struct ConnectionRoutePoint
{
    double x = 0.0;
    double y = 0.0;
};

// A connection connecting an output/bidirectional port of one subsystem to an input port of another
struct SignalConnection
{
    // Source endpoint (e.g. "controllerA.drive" or "system.power")
    std::string fromInstance;
    std::string fromPort;

    // Destination endpoint (e.g. "motorA.voltage_in")
    std::string toInstance;
    std::string toPort;

    std::string netName;            // Optional shared net/bus name
    std::string connectionId;       // Stable graphical/editor identity
    std::vector<ConnectionRoutePoint> routePoints; // Visual-only routing waypoints

    std::string sourcePath() const { return fromInstance + "." + fromPort; }
    std::string destPath() const { return toInstance + "." + toPort; }

    juce::var toJson() const;
    static SignalConnection fromJson(const juce::var& json);
};

// -----------------------------------------------------------------------------
// SystemAssembly Document
// -----------------------------------------------------------------------------

struct SystemAssembly
{
    std::string name = "System Assembly";
    std::string description;
    std::string version = "1.0";

    // Reusable subsystem definitions included or defined in this assembly
    std::vector<SubsystemDefinition> definitions;

    // Placed instances
    std::vector<SubsystemInstance> instances;

    // Interconnections between instance ports
    std::vector<SignalConnection> connections;

    // External system-level ports/signals (exposed for operator panels or higher-level nesting)
    std::vector<SubsystemPort> systemPorts;

    // P6: Associated mechanical visualization scene
    mechanical_scene::MechanicalScene mechanicalScene;

    // Helper queries
    const SubsystemDefinition* findDefinition(const std::string& defId) const;
    const SubsystemInstance* findInstance(const std::string& instId) const;

    // Pre-built definitions factory
    static SubsystemDefinition makeDCMotorDefinition();
    static SubsystemDefinition makeMotorControllerDefinition();
    static SubsystemDefinition makeCoordinatedSequencerDefinition();
    static SubsystemDefinition makePlotPanelDefinition(int channelCount = 4);

    // Standard Dual-Motor Coordinated System assembly factory
    static SystemAssembly createDualMotorSystem();

    // P5 Demonstration: User-Assembled Motor Control System
    // (Reusable schematic motor-drive subsystems + reusable Node Designer/FRust controller)
    static SystemAssembly createUserAssembledMotorSystem();

    // P6 Demonstration B: Controlled Mechanical Robotic Arm System
    static SystemAssembly createControlledArmSystem();

    // Validation: verifies instances, definitions, connections, and circular dependencies
    std::vector<std::string> validate(const juce::File& projectFolder = {}) const;

    // Signal discovery: enumerates all observable hierarchical signals
    std::vector<std::string> discoverSignals() const;

    // JSON serialization
    juce::var toJson() const;
    static SystemAssembly fromJson(const juce::var& json);

    bool saveToFile(const juce::File& file, juce::String& error) const;
    static bool loadFromFile(const juce::File& file, SystemAssembly& out, juce::String& error);
};

// -----------------------------------------------------------------------------
// Assembly Simulation Builder
// -----------------------------------------------------------------------------

// Result of compiling an assembly into an MNA simulation circuit and signal bindings
struct CompiledAssemblyCircuit
{
    circuit_sim::Circuit circuit;

    // Map from hierarchical signal path (e.g. "motorA.speed", "system.power")
    // to corresponding circuit Node or Branch element
    std::map<std::string, circuit_sim::Node> signalNodes;
    std::map<std::string, int> signalBranches;

    // Input elements that can be driven by operator controls (e.g. "system.power", "motorA.load_torque")
    std::map<std::string, int> inputElements;

    // Signal metadata (units, range, direction)
    struct SignalMeta
    {
        std::string path;
        PortDomain domain = PortDomain::Electrical;
        std::string unit;
        double minVal = 0.0;
        double maxVal = 100.0;
        bool isInput = false;
    };
    std::vector<SignalMeta> metadata;

    std::string error;
    bool success = false;
};

// Compiler that compiles a SystemAssembly into an executable MNA Circuit
class AssemblyCompiler
{
public:
    static CompiledAssemblyCircuit compile(const SystemAssembly& assembly, const juce::File& projectFolder = {});
};

} // namespace sim_panel

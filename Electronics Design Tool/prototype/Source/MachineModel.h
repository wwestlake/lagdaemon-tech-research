#pragma once

#include "SystemAssembly.h"
#include "MechanicalSceneModel.h"
#include "ControlPanelModel.h"
#include "SystemSimulationSession.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace machine_builder
{

// -----------------------------------------------------------------------------
// Machine Project Model
// -----------------------------------------------------------------------------

struct SimulationConfig
{
    double defaultRate = 1.0;
    double stepSize = 0.001; // 1 ms
    bool autoStart = false;
    std::map<std::string, double> initialNumericInputs;
    std::map<std::string, bool> initialBooleanInputs;

    juce::var toJson() const;
    static SimulationConfig fromJson(const juce::var& json);
};

struct Diagnostic
{
    enum class Level { Warning, Error };

    Level level = Level::Error;
    std::string component;  // "assembly", "scene", "control_panel", "binding"
    std::string targetId;   // "motorA", "joint_shoulder", "btn_start"
    std::string message;

    juce::var toJson() const;
    static Diagnostic fromJson(const juce::var& json);
};

struct MachineDesign
{
    std::string id = "machine_project";
    std::string name = "Virtual Machine";
    std::string description;
    std::string version = "1.0";
    std::string author;

    // Integrated core models
    sim_panel::SystemAssembly assembly;
    mechanical_scene::MechanicalScene scene;
    sim_panel::ControlPanelLayout controlPanel;
    SimulationConfig simConfig;

    // Validation diagnostics
    std::vector<Diagnostic> validate(const juce::File& projectFolder = {}) const;
    bool isValid(const juce::File& projectFolder = {}) const;

    // Subsystem assembly editing
    bool addSubsystemInstance(const std::string& instanceId, const std::string& definitionId,
                             const std::string& title, const std::map<std::string, double>& numericParams = {},
                             std::string* error = nullptr);
    bool removeSubsystemInstance(const std::string& instanceId, std::string* error = nullptr);
    bool setInstanceParameter(const std::string& instanceId, const std::string& paramName,
                             double value, std::string* error = nullptr);
    bool connectPorts(const std::string& fromInstance, const std::string& fromPort,
                      const std::string& toInstance, const std::string& toPort,
                      const std::string& netName = "", std::string* error = nullptr);
    bool disconnectPorts(const std::string& fromInstance, const std::string& fromPort,
                         const std::string& toInstance, const std::string& toPort,
                         std::string* error = nullptr);

    // Mechanical scene editing
    bool addRigidBody(const std::string& bodyId, const std::string& name,
                      const std::string& parentId = "",
                      const mechanical_scene::Vec3& localPosition = {},
                      std::string* error = nullptr);
    bool removeRigidBody(const std::string& bodyId, std::string* error = nullptr);
    bool addVisualGeometry(const std::string& bodyId, mechanical_scene::PrimitiveType type,
                           const mechanical_scene::Vec3& size, const mechanical_scene::Vec3& offset,
                           juce::Colour colour, juce::Colour accentColour = juce::Colour(0),
                           std::string* error = nullptr);
    bool addJoint(const std::string& jointId, const std::string& name,
                  mechanical_scene::JointType type, const std::string& bodyId,
                  const mechanical_scene::Vec3& axis, bool hasLimits = false,
                  double minLimit = 0.0, double maxLimit = 0.0,
                  std::string* error = nullptr);
    bool removeJoint(const std::string& jointId, std::string* error = nullptr);
    bool bindJointSignal(const std::string& jointId, const std::string& signalName,
                         double scale = 1.0, double offset = 0.0,
                         bool isVelocity = false, std::string* error = nullptr);

    // Control panel widget editing
    bool addControlWidget(const sim_panel::WidgetConfig& widget, std::string* error = nullptr);
    bool removeControlWidget(const std::string& widgetId, std::string* error = nullptr);

    // Runtime compilation
    bool compileAndLoadIntoSession(sim_panel::SystemSimulationSession& session, std::string* error = nullptr,
                                   const juce::File& projectFolder = {}) const;

    // JSON serialization
    juce::var toJson() const;
    static MachineDesign fromJson(const juce::var& json);

    // File persistence
    bool saveToFile(const juce::File& file, juce::String& error) const;
    static bool loadFromFile(const juce::File& file, MachineDesign& out, juce::String& error);

    // Factory templates built purely via the generic builder methods
    static MachineDesign createBlankMachine(const std::string& name = "New Machine");
    static MachineDesign createDualMotorMachineTemplate();
    static MachineDesign createRoboticArmMachineTemplate();
};

} // namespace machine_builder

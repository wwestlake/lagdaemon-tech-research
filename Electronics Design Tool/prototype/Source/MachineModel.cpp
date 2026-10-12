#include "MachineModel.h"

#include <set>
#include <algorithm>

namespace machine_builder
{
namespace
{
juce::File resolveReferenceFile(const std::string& referenceFile, const juce::File& projectFolder)
{
    if (referenceFile.empty())
        return {};

    const juce::String ref(referenceFile);
    if (juce::File::isAbsolutePath(ref))
        return juce::File(ref);

    if (projectFolder != juce::File())
        return projectFolder.getChildFile(ref);

    return juce::File(ref);
}

bool hasSystemPort(const sim_panel::SystemAssembly& assembly, const std::string& portName)
{
    return std::any_of(assembly.systemPorts.begin(), assembly.systemPorts.end(),
                       [&](const sim_panel::SubsystemPort& p) { return p.name == portName; });
}
}

// -----------------------------------------------------------------------------
// SimulationConfig JSON
// -----------------------------------------------------------------------------

juce::var SimulationConfig::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("defaultRate", defaultRate);
    obj->setProperty("stepSize", stepSize);
    obj->setProperty("autoStart", autoStart);

    auto* numObj = new juce::DynamicObject();
    for (const auto& pair : initialNumericInputs)
        numObj->setProperty(juce::String(pair.first), pair.second);
    obj->setProperty("initialNumericInputs", juce::var(numObj));

    auto* boolObj = new juce::DynamicObject();
    for (const auto& pair : initialBooleanInputs)
        boolObj->setProperty(juce::String(pair.first), pair.second);
    obj->setProperty("initialBooleanInputs", juce::var(boolObj));

    return juce::var(obj);
}

SimulationConfig SimulationConfig::fromJson(const juce::var& json)
{
    SimulationConfig cfg;
    if (const auto* obj = json.getDynamicObject())
    {
        cfg.defaultRate = obj->hasProperty("defaultRate") ? (double)obj->getProperty("defaultRate") : 1.0;
        cfg.stepSize = obj->hasProperty("stepSize") ? (double)obj->getProperty("stepSize") : 0.001;
        cfg.autoStart = obj->hasProperty("autoStart") ? (bool)obj->getProperty("autoStart") : false;

        if (const auto* numObj = obj->getProperty("initialNumericInputs").getDynamicObject())
            for (const auto& prop : numObj->getProperties())
                cfg.initialNumericInputs[prop.name.toString().toStdString()] = (double)prop.value;

        if (const auto* boolObj = obj->getProperty("initialBooleanInputs").getDynamicObject())
            for (const auto& prop : boolObj->getProperties())
                cfg.initialBooleanInputs[prop.name.toString().toStdString()] = (bool)prop.value;
    }
    return cfg;
}

// -----------------------------------------------------------------------------
// Diagnostic JSON
// -----------------------------------------------------------------------------

juce::var Diagnostic::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("level", level == Level::Warning ? "warning" : "error");
    obj->setProperty("component", juce::String(component));
    obj->setProperty("targetId", juce::String(targetId));
    obj->setProperty("message", juce::String(message));
    return juce::var(obj);
}

Diagnostic Diagnostic::fromJson(const juce::var& json)
{
    Diagnostic d;
    if (const auto* obj = json.getDynamicObject())
    {
        d.level = obj->getProperty("level").toString().toLowerCase() == "warning" ? Level::Warning : Level::Error;
        d.component = obj->getProperty("component").toString().toStdString();
        d.targetId = obj->getProperty("targetId").toString().toStdString();
        d.message = obj->getProperty("message").toString().toStdString();
    }
    return d;
}

// -----------------------------------------------------------------------------
// Validation
// -----------------------------------------------------------------------------

std::vector<Diagnostic> MachineDesign::validate(const juce::File& projectFolder) const
{
    std::vector<Diagnostic> diags;

    // 1. Validate Assembly
    std::set<std::string> instanceIds;
    for (const auto& inst : assembly.instances)
    {
        if (inst.instanceId.empty())
        {
            diags.push_back({ Diagnostic::Level::Error, "assembly", "", "Subsystem instance has empty instanceId." });
            continue;
        }
        if (instanceIds.count(inst.instanceId) > 0)
        {
            diags.push_back({ Diagnostic::Level::Error, "assembly", inst.instanceId, "Duplicate subsystem instance ID: " + inst.instanceId });
        }
        instanceIds.insert(inst.instanceId);

        const auto* def = assembly.findDefinition(inst.definitionId);
        if (def == nullptr)
        {
            diags.push_back({ Diagnostic::Level::Error, "assembly", inst.instanceId,
                              "Referenced definition not found: " + inst.definitionId });
        }
        else if ((def->kind == sim_panel::SubsystemKind::SchematicReference
               || def->kind == sim_panel::SubsystemKind::ProgrammableComponent)
              && !def->referenceFile.empty())
        {
            if (projectFolder != juce::File())
            {
                const auto ref = resolveReferenceFile(def->referenceFile, projectFolder);
                if (!ref.existsAsFile())
                    diags.push_back({ Diagnostic::Level::Error, "assembly", inst.instanceId,
                                      "Referenced subsystem file not found: " + def->referenceFile });
            }
        }
    }

    // 2. Validate Connections
    for (const auto& conn : assembly.connections)
    {
        const auto* fromInst = conn.fromInstance == "system" ? nullptr : assembly.findInstance(conn.fromInstance);
        const auto* toInst = conn.toInstance == "system" ? nullptr : assembly.findInstance(conn.toInstance);

        if (conn.fromInstance == "system")
        {
            if (!hasSystemPort(assembly, conn.fromPort))
                diags.push_back({ Diagnostic::Level::Error, "assembly", conn.sourcePath(),
                                  "Connection source system port not found: system." + conn.fromPort });
        }
        else if (fromInst == nullptr)
        {
            diags.push_back({ Diagnostic::Level::Error, "assembly", conn.fromInstance,
                              "Connection source instance not found: " + conn.fromInstance });
        }
        else
        {
            const auto* def = assembly.findDefinition(fromInst->definitionId);
            if (def != nullptr && def->findPort(conn.fromPort) == nullptr)
            {
                diags.push_back({ Diagnostic::Level::Error, "assembly", conn.sourcePath(),
                                  "Source port '" + conn.fromPort + "' not found on definition '" + def->id + "'" });
            }
        }

        if (conn.toInstance == "system")
        {
            if (!hasSystemPort(assembly, conn.toPort))
                diags.push_back({ Diagnostic::Level::Error, "assembly", conn.destPath(),
                                  "Connection destination system port not found: system." + conn.toPort });
        }
        else if (toInst == nullptr)
        {
            diags.push_back({ Diagnostic::Level::Error, "assembly", conn.toInstance,
                              "Connection destination instance not found: " + conn.toInstance });
        }
        else
        {
            const auto* def = assembly.findDefinition(toInst->definitionId);
            if (def != nullptr && def->findPort(conn.toPort) == nullptr)
            {
                diags.push_back({ Diagnostic::Level::Error, "assembly", conn.destPath(),
                                  "Destination port '" + conn.toPort + "' not found on definition '" + def->id + "'" });
            }
        }
    }

    // 3. Validate Mechanical Scene
    std::set<std::string> bodyIds;
    for (const auto& b : scene.bodies)
    {
        if (b.id.empty())
        {
            diags.push_back({ Diagnostic::Level::Error, "scene", "", "RigidBody has empty ID." });
            continue;
        }
        if (bodyIds.count(b.id) > 0)
        {
            diags.push_back({ Diagnostic::Level::Error, "scene", b.id, "Duplicate RigidBody ID: " + b.id });
        }
        bodyIds.insert(b.id);
    }

    // Check cyclic parenting
    for (const auto& b : scene.bodies)
    {
        std::set<std::string> visited;
        std::string curr = b.parentId;
        while (!curr.empty())
        {
            if (curr == b.id || visited.count(curr) > 0)
            {
                diags.push_back({ Diagnostic::Level::Error, "scene", b.id, "Cyclic parenting detected starting at body: " + b.id });
                break;
            }
            visited.insert(curr);
            const auto* parentBody = scene.findBody(curr);
            curr = parentBody ? parentBody->parentId : "";
        }
    }

    // Validate Joints
    std::set<std::string> jointIds;
    const auto availableSignals = assembly.discoverSignals();
    const std::set<std::string> signalSet(availableSignals.begin(), availableSignals.end());

    for (const auto& j : scene.joints)
    {
        if (j.id.empty())
        {
            diags.push_back({ Diagnostic::Level::Error, "scene", "", "Joint has empty ID." });
            continue;
        }
        if (jointIds.count(j.id) > 0)
        {
            diags.push_back({ Diagnostic::Level::Error, "scene", j.id, "Duplicate Joint ID: " + j.id });
        }
        jointIds.insert(j.id);

        if (j.limits.enabled && j.limits.minVal > j.limits.maxVal)
        {
            diags.push_back({ Diagnostic::Level::Error, "scene", j.id,
                              "Joint travel limit invalid: min (" + std::to_string(j.limits.minVal) +
                              ") > max (" + std::to_string(j.limits.maxVal) + ")" });
        }

        // Validate signal binding
        if (!j.binding.signalName.empty())
        {
            if (signalSet.count(j.binding.signalName) == 0 &&
                (j.binding.fallbackSignal.empty() || signalSet.count(j.binding.fallbackSignal) == 0))
            {
                diags.push_back({ Diagnostic::Level::Warning, "binding", j.id,
                                  "Joint bound to signal '" + j.binding.signalName + "' which is not currently discovered in assembly." });
            }
        }
    }

    // 4. Validate Control Panel
    std::set<std::string> widgetIds;
    for (const auto& w : controlPanel.widgets)
    {
        if (w.id.empty())
        {
            diags.push_back({ Diagnostic::Level::Error, "control_panel", "", "Control widget has empty ID." });
            continue;
        }
        if (widgetIds.count(w.id) > 0)
        {
            diags.push_back({ Diagnostic::Level::Error, "control_panel", w.id, "Duplicate control widget ID: " + w.id });
        }
        widgetIds.insert(w.id);

        if (!w.signalName.empty() && signalSet.count(w.signalName) == 0)
        {
            diags.push_back({ Diagnostic::Level::Warning, "control_panel", w.id,
                              "Widget '" + w.id + "' binds to signal '" + w.signalName + "' which is not currently discovered in assembly." });
        }
    }

    return diags;
}

bool MachineDesign::isValid(const juce::File& projectFolder) const
{
    const auto diags = validate(projectFolder);
    for (const auto& d : diags)
        if (d.level == Diagnostic::Level::Error)
            return false;
    return true;
}

// -----------------------------------------------------------------------------
// Subsystem Editing Methods
// -----------------------------------------------------------------------------

bool MachineDesign::addSubsystemInstance(const std::string& instanceId, const std::string& definitionId,
                                        const std::string& title, const std::map<std::string, double>& numericParams,
                                        std::string* error)
{
    if (instanceId.empty())
    {
        if (error) *error = "Instance ID cannot be empty.";
        return false;
    }
    if (assembly.findInstance(instanceId) != nullptr)
    {
        if (error) *error = "Subsystem instance '" + instanceId + "' already exists.";
        return false;
    }

    sim_panel::SubsystemInstance inst;
    inst.instanceId = instanceId;
    inst.definitionId = definitionId;
    inst.title = title.empty() ? instanceId : title;
    inst.numericParams = numericParams;
    assembly.instances.push_back(inst);
    return true;
}

bool MachineDesign::removeSubsystemInstance(const std::string& instanceId, std::string* error)
{
    auto it = std::find_if(assembly.instances.begin(), assembly.instances.end(),
                           [&](const sim_panel::SubsystemInstance& i) { return i.instanceId == instanceId; });
    if (it == assembly.instances.end())
    {
        if (error) *error = "Subsystem instance '" + instanceId + "' not found.";
        return false;
    }
    assembly.instances.erase(it);

    // Remove any connected ports
    assembly.connections.erase(
        std::remove_if(assembly.connections.begin(), assembly.connections.end(),
                       [&](const sim_panel::SignalConnection& c) {
                           return c.fromInstance == instanceId || c.toInstance == instanceId;
                       }),
        assembly.connections.end());
    return true;
}

bool MachineDesign::setInstanceParameter(const std::string& instanceId, const std::string& paramName,
                                        double value, std::string* error)
{
    for (auto& inst : assembly.instances)
    {
        if (inst.instanceId == instanceId)
        {
            inst.numericParams[paramName] = value;
            return true;
        }
    }
    if (error) *error = "Subsystem instance '" + instanceId + "' not found.";
    return false;
}

bool MachineDesign::connectPorts(const std::string& fromInstance, const std::string& fromPort,
                                const std::string& toInstance, const std::string& toPort,
                                const std::string& netName, std::string* error)
{
    if (assembly.findInstance(fromInstance) == nullptr)
    {
        if (error) *error = "Source instance '" + fromInstance + "' not found.";
        return false;
    }
    if (assembly.findInstance(toInstance) == nullptr)
    {
        if (error) *error = "Destination instance '" + toInstance + "' not found.";
        return false;
    }

    // Check for existing connection between these ports
    for (const auto& c : assembly.connections)
    {
        if (c.fromInstance == fromInstance && c.fromPort == fromPort &&
            c.toInstance == toInstance && c.toPort == toPort)
        {
            return true; // Already connected
        }
    }

    sim_panel::SignalConnection conn;
    conn.fromInstance = fromInstance;
    conn.fromPort = fromPort;
    conn.toInstance = toInstance;
    conn.toPort = toPort;
    conn.netName = netName.empty() ? ("NET_" + fromInstance + "_" + fromPort) : netName;
    assembly.connections.push_back(conn);
    return true;
}

bool MachineDesign::disconnectPorts(const std::string& fromInstance, const std::string& fromPort,
                                   const std::string& toInstance, const std::string& toPort,
                                   std::string* error)
{
    auto it = std::find_if(assembly.connections.begin(), assembly.connections.end(),
                           [&](const sim_panel::SignalConnection& c) {
                               return c.fromInstance == fromInstance && c.fromPort == fromPort &&
                                      c.toInstance == toInstance && c.toPort == toPort;
                           });
    if (it == assembly.connections.end())
    {
        if (error) *error = "Connection not found.";
        return false;
    }
    assembly.connections.erase(it);
    return true;
}

// -----------------------------------------------------------------------------
// Mechanical Scene Editing Methods
// -----------------------------------------------------------------------------

bool MachineDesign::addRigidBody(const std::string& bodyId, const std::string& name,
                                const std::string& parentId,
                                const mechanical_scene::Vec3& localPosition,
                                std::string* error)
{
    if (bodyId.empty())
    {
        if (error) *error = "Body ID cannot be empty.";
        return false;
    }
    if (scene.findBody(bodyId) != nullptr)
    {
        if (error) *error = "RigidBody '" + bodyId + "' already exists.";
        return false;
    }

    mechanical_scene::RigidBody body;
    body.id = bodyId;
    body.name = name.empty() ? bodyId : name;
    body.parentId = parentId;
    body.localPosition = localPosition;
    scene.bodies.push_back(body);
    scene.evaluateTransforms();
    return true;
}

bool MachineDesign::removeRigidBody(const std::string& bodyId, std::string* error)
{
    auto it = std::find_if(scene.bodies.begin(), scene.bodies.end(),
                           [&](const mechanical_scene::RigidBody& b) { return b.id == bodyId; });
    if (it == scene.bodies.end())
    {
        if (error) *error = "RigidBody '" + bodyId + "' not found.";
        return false;
    }
    scene.bodies.erase(it);

    // Reparent or disconnect child bodies
    for (auto& b : scene.bodies)
        if (b.parentId == bodyId)
            b.parentId = "";

    scene.evaluateTransforms();
    return true;
}

bool MachineDesign::addVisualGeometry(const std::string& bodyId, mechanical_scene::PrimitiveType type,
                                     const mechanical_scene::Vec3& size, const mechanical_scene::Vec3& offset,
                                     juce::Colour colour, juce::Colour accentColour, std::string* error)
{
    auto* body = scene.findBody(bodyId);
    if (body == nullptr)
    {
        if (error) *error = "RigidBody '" + bodyId + "' not found.";
        return false;
    }

    mechanical_scene::VisualGeometry geo;
    geo.type = type;
    geo.size = size;
    geo.offset = offset;
    geo.colour = colour;
    geo.accentColour = accentColour;
    body->geometries.push_back(geo);
    return true;
}

bool MachineDesign::addJoint(const std::string& jointId, const std::string& name,
                            mechanical_scene::JointType type, const std::string& bodyId,
                            const mechanical_scene::Vec3& axis, bool hasLimits,
                            double minLimit, double maxLimit, std::string* error)
{
    if (jointId.empty())
    {
        if (error) *error = "Joint ID cannot be empty.";
        return false;
    }
    if (scene.findJoint(jointId) != nullptr)
    {
        if (error) *error = "Joint '" + jointId + "' already exists.";
        return false;
    }

    mechanical_scene::Joint j;
    j.id = jointId;
    j.name = name.empty() ? jointId : name;
    j.type = type;
    j.axis = axis;
    j.limits.enabled = hasLimits;
    j.limits.minVal = minLimit;
    j.limits.maxVal = maxLimit;
    scene.joints.push_back(j);

    if (!bodyId.empty())
    {
        if (auto* b = scene.findBody(bodyId))
            b->jointId = jointId;
    }

    scene.evaluateTransforms();
    return true;
}

bool MachineDesign::removeJoint(const std::string& jointId, std::string* error)
{
    auto it = std::find_if(scene.joints.begin(), scene.joints.end(),
                           [&](const mechanical_scene::Joint& j) { return j.id == jointId; });
    if (it == scene.joints.end())
    {
        if (error) *error = "Joint '" + jointId + "' not found.";
        return false;
    }
    scene.joints.erase(it);

    for (auto& b : scene.bodies)
        if (b.jointId == jointId)
            b.jointId = "";

    scene.evaluateTransforms();
    return true;
}

bool MachineDesign::bindJointSignal(const std::string& jointId, const std::string& signalName,
                                   double scale, double offset, bool isVelocity, std::string* error)
{
    auto* j = scene.findJoint(jointId);
    if (j == nullptr)
    {
        if (error) *error = "Joint '" + jointId + "' not found.";
        return false;
    }
    j->binding.signalName = signalName;
    j->binding.scale = scale;
    j->binding.offset = offset;
    j->binding.isVelocity = isVelocity;
    return true;
}

// -----------------------------------------------------------------------------
// Control Widget Editing Methods
// -----------------------------------------------------------------------------

bool MachineDesign::addControlWidget(const sim_panel::WidgetConfig& widget, std::string* error)
{
    if (widget.id.empty())
    {
        if (error) *error = "Widget ID cannot be empty.";
        return false;
    }
    for (const auto& w : controlPanel.widgets)
    {
        if (w.id == widget.id)
        {
            if (error) *error = "Widget '" + widget.id + "' already exists.";
            return false;
        }
    }
    controlPanel.widgets.push_back(widget);
    return true;
}

bool MachineDesign::removeControlWidget(const std::string& widgetId, std::string* error)
{
    auto it = std::find_if(controlPanel.widgets.begin(), controlPanel.widgets.end(),
                           [&](const sim_panel::WidgetConfig& w) { return w.id == widgetId; });
    if (it == controlPanel.widgets.end())
    {
        if (error) *error = "Widget '" + widgetId + "' not found.";
        return false;
    }
    controlPanel.widgets.erase(it);
    return true;
}

// -----------------------------------------------------------------------------
// Compilation & Session Integration
// -----------------------------------------------------------------------------

bool MachineDesign::compileAndLoadIntoSession(sim_panel::SystemSimulationSession& session, std::string* error,
                                              const juce::File& projectFolder) const
{
    auto asmCopy = assembly;
    asmCopy.mechanicalScene = scene;

    if (!session.setupAssembly(asmCopy, projectFolder))
    {
        if (error) *error = "Failed to compile assembly into simulation session.";
        return false;
    }

    session.setExecutionRate(simConfig.defaultRate);

    // Apply initial inputs
    for (const auto& pair : simConfig.initialNumericInputs)
        session.setNumericInput(pair.first, pair.second);

    for (const auto& pair : simConfig.initialBooleanInputs)
        session.setBooleanInput(pair.first, pair.second);

    if (simConfig.autoStart)
        session.start();

    return true;
}

// -----------------------------------------------------------------------------
// JSON Serialization & File I/O
// -----------------------------------------------------------------------------

juce::var MachineDesign::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("name", juce::String(name));
    obj->setProperty("description", juce::String(description));
    obj->setProperty("version", juce::String(version));
    obj->setProperty("author", juce::String(author));

    obj->setProperty("assembly", assembly.toJson());
    obj->setProperty("scene", scene.toJson());
    obj->setProperty("controlPanel", controlPanel.toJson());
    obj->setProperty("simConfig", simConfig.toJson());

    return juce::var(obj);
}

MachineDesign MachineDesign::fromJson(const juce::var& json)
{
    MachineDesign m;
    if (const auto* obj = json.getDynamicObject())
    {
        m.id = obj->getProperty("id").toString().toStdString();
        m.name = obj->getProperty("name").toString().toStdString();
        m.description = obj->getProperty("description").toString().toStdString();
        m.version = obj->getProperty("version").toString().toStdString();
        m.author = obj->getProperty("author").toString().toStdString();

        m.assembly = sim_panel::SystemAssembly::fromJson(obj->getProperty("assembly"));
        m.scene = mechanical_scene::MechanicalScene::fromJson(obj->getProperty("scene"));
        m.controlPanel = sim_panel::ControlPanelLayout::fromJson(obj->getProperty("controlPanel"));
        m.simConfig = SimulationConfig::fromJson(obj->getProperty("simConfig"));
    }
    return m;
}

bool MachineDesign::saveToFile(const juce::File& file, juce::String& error) const
{
    const auto jsonVar = toJson();
    const auto str = juce::JSON::toString(jsonVar, true);
    if (!file.replaceWithText(str))
    {
        error = "Failed to write machine file: " + file.getFullPathName();
        return false;
    }
    return true;
}

bool MachineDesign::loadFromFile(const juce::File& file, MachineDesign& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Machine file not found: " + file.getFullPathName();
        return false;
    }
    const auto content = file.loadFileAsString();
    const auto parsed = juce::JSON::parse(content);
    if (!parsed.isObject())
    {
        error = "Invalid machine JSON syntax in: " + file.getFullPathName();
        return false;
    }
    out = fromJson(parsed);
    const auto base = file.getParentDirectory();
    for (auto& def : out.assembly.definitions)
        if (!def.referenceFile.empty() && !juce::File::isAbsolutePath(juce::String(def.referenceFile)))
            def.referenceFile = base.getChildFile(juce::String(def.referenceFile)).getFullPathName().toStdString();
    return true;
}

// -----------------------------------------------------------------------------
// Factory Templates (constructed purely via the generic builder methods)
// -----------------------------------------------------------------------------

MachineDesign MachineDesign::createBlankMachine(const std::string& name)
{
    MachineDesign m;
    m.id = "blank_machine";
    m.name = name;
    m.description = "New virtual machine built from reusable subsystems and mechanical geometry.";
    m.version = "1.0";

    m.assembly.name = name + " Assembly";
    m.scene.id = "scene_blank";
    m.scene.name = name + " Scene";
    m.controlPanel.title = name + " Controls";
    return m;
}

MachineDesign MachineDesign::createDualMotorMachineTemplate()
{
    MachineDesign m;
    m.id = "dual_motor_machine";
    m.name = "Coordinated Dual-Motor System";
    m.description = "Coordinated dual-motor system with independent closed-loop velocity controllers, "
                    "delayed start sequencing, load disturbance isolation, and master emergency stop.";
    m.version = "1.0";
    // 1. Definitions
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeDCMotorDefinition());
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeMotorControllerDefinition());
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeCoordinatedSequencerDefinition());

    // 2. Add Subsystem Instances using the builder API
    m.addSubsystemInstance("motorA", "dc_motor", "Drive Motor A",
                           { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00001 }, { "b", 0.000001 } });
    m.addSubsystemInstance("motorB", "dc_motor", "Drive Motor B",
                           { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00001 }, { "b", 0.000001 } });
    m.addSubsystemInstance("controllerA", "motor_speed_controller", "Speed Controller A",
                           { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } });
    m.addSubsystemInstance("controllerB", "motor_speed_controller", "Speed Controller B",
                           { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } });
    m.addSubsystemInstance("sequencer", "coordinated_sequencer", "Coordinated Sequencer",
                           { { "motor_b_delay_sec", 0.15 } });

    // 3. Connect Ports using the builder API
    m.connectPorts("sequencer", "start_a_out", "controllerA", "start_in", "NET_START_A");
    m.connectPorts("sequencer", "start_b_out", "controllerB", "start_in", "NET_START_B");
    m.connectPorts("sequencer", "stop_all_out", "controllerA", "stop_in", "NET_STOP_A");
    m.connectPorts("sequencer", "stop_all_out", "controllerB", "stop_in", "NET_STOP_B");
    m.connectPorts("sequencer", "estop_all_out", "controllerA", "estop_in", "NET_ESTOP");
    m.connectPorts("sequencer", "estop_all_out", "controllerB", "estop_in", "NET_ESTOP");

    m.connectPorts("controllerA", "drive_out", "motorA", "v_in", "NET_DRIVE_A");
    m.connectPorts("controllerB", "drive_out", "motorB", "v_in", "NET_DRIVE_B");
    m.connectPorts("motorA", "tach", "controllerA", "feedback_in", "NET_TACH_A");
    m.connectPorts("motorB", "tach", "controllerB", "feedback_in", "NET_TACH_B");

    // 4. Build Mechanical Scene using the builder API
    m.scene.id = "dual_motor_scene";
    m.scene.name = "Dual Motor Mechanical Assembly";

    m.addRigidBody("bedplate", "Machine Bedplate", "", { 0.0, 0.0, 0.0 });
    m.addVisualGeometry("bedplate", mechanical_scene::PrimitiveType::Box,
                       { 1.8, 0.1, 0.9 }, { 0.0, -0.05, 0.0 }, juce::Colour(0xff18222b));

    m.addRigidBody("motorA_stator", "Motor A Stator", "bedplate", { -0.5, 0.2, 0.0 });
    m.addVisualGeometry("motorA_stator", mechanical_scene::PrimitiveType::Cylinder,
                       { 0.18, 0.35, 0.18 }, { 0.0, 0.0, 0.0 }, juce::Colour(0xff2d4052));

    m.addJoint("joint_motorA", "Motor A Rotor Joint", mechanical_scene::JointType::Revolute,
               "", { 0.0, 0.0, 1.0 });
    m.bindJointSignal("joint_motorA", "motorA.angle", 1.0, 0.0, true);

    m.addRigidBody("motorA_rotor", "Motor A Shaft & Wheel", "motorA_stator", { 0.0, 0.0, 0.25 });
    m.scene.findBody("motorA_rotor")->jointId = "joint_motorA";
    m.addVisualGeometry("motorA_rotor", mechanical_scene::PrimitiveType::Cylinder,
                       { 0.04, 0.2, 0.04 }, { 0.0, 0.0, 0.0 }, juce::Colour(0xffb0c4de));
    m.addVisualGeometry("motorA_rotor", mechanical_scene::PrimitiveType::Wheel,
                       { 0.25, 0.05, 0.25 }, { 0.0, 0.0, 0.1 }, juce::Colour(0xff35c6ff), juce::Colour(0xffffa630));

    m.addRigidBody("motorB_stator", "Motor B Stator", "bedplate", { 0.5, 0.2, 0.0 });
    m.addVisualGeometry("motorB_stator", mechanical_scene::PrimitiveType::Cylinder,
                       { 0.18, 0.35, 0.18 }, { 0.0, 0.0, 0.0 }, juce::Colour(0xff2d4052));

    m.addJoint("joint_motorB", "Motor B Rotor Joint", mechanical_scene::JointType::Revolute,
               "", { 0.0, 0.0, 1.0 });
    m.bindJointSignal("joint_motorB", "motorB.angle", 1.0, 0.0, true);

    m.addRigidBody("motorB_rotor", "Motor B Shaft & Wheel", "motorB_stator", { 0.0, 0.0, 0.25 });
    m.scene.findBody("motorB_rotor")->jointId = "joint_motorB";
    m.addVisualGeometry("motorB_rotor", mechanical_scene::PrimitiveType::Cylinder,
                       { 0.04, 0.2, 0.04 }, { 0.0, 0.0, 0.0 }, juce::Colour(0xffb0c4de));
    m.addVisualGeometry("motorB_rotor", mechanical_scene::PrimitiveType::Wheel,
                       { 0.25, 0.05, 0.25 }, { 0.0, 0.0, 0.1 }, juce::Colour(0xff80ed99), juce::Colour(0xffff6b6b));

    // 5. Build Control Panel Layout using builder API
    m.controlPanel = sim_panel::ControlPanelLayout::createDualMotorCoordinatedPanel();

    return m;
}

MachineDesign MachineDesign::createRoboticArmMachineTemplate()
{
    MachineDesign m;
    m.id = "robotic_arm_machine";
    m.name = "Articulated Robotic Arm (2-DOF)";
    m.description = "Two-link articulated mechanical arm driven by closed-loop position controllers with joint limits.";
    m.version = "1.0";

    // 1. Reusable Definitions
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeDCMotorDefinition());
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeMotorControllerDefinition());
    m.assembly.definitions.push_back(sim_panel::SystemAssembly::makeCoordinatedSequencerDefinition());

    // 2. Add Subsystem Instances using the builder API
    m.addSubsystemInstance("motorA", "dc_motor", "Shoulder Actuator",
                           { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00005 }, { "b", 0.00001 } });
    m.addSubsystemInstance("motorB", "dc_motor", "Elbow Actuator",
                           { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00002 }, { "b", 0.00001 } });
    m.addSubsystemInstance("controllerA", "motor_speed_controller", "Shoulder Position/Velocity Controller",
                           { { "kp", 0.1 }, { "ki", 1.5 }, { "kd", 0.0005 }, { "v_max", 12.0 }, { "v_min", 0.0 } });
    m.addSubsystemInstance("controllerB", "motor_speed_controller", "Elbow Position/Velocity Controller",
                           { { "kp", 0.1 }, { "ki", 1.5 }, { "kd", 0.0005 }, { "v_max", 12.0 }, { "v_min", 0.0 } });
    m.addSubsystemInstance("sequencer", "coordinated_sequencer", "Arm Homing & Startup Sequencer",
                           { { "motor_b_delay_sec", 0.1 } });

    // 3. Connect Ports using the builder API
    m.connectPorts("sequencer", "start_a_out", "controllerA", "start_in", "NET_START_A");
    m.connectPorts("sequencer", "start_b_out", "controllerB", "start_in", "NET_START_B");
    m.connectPorts("sequencer", "stop_all_out", "controllerA", "stop_in", "NET_STOP_A");
    m.connectPorts("sequencer", "stop_all_out", "controllerB", "stop_in", "NET_STOP_B");
    m.connectPorts("sequencer", "estop_all_out", "controllerA", "estop_in", "NET_ESTOP");
    m.connectPorts("sequencer", "estop_all_out", "controllerB", "estop_in", "NET_ESTOP");

    m.connectPorts("controllerA", "drive_out", "motorA", "v_in", "NET_DRIVE_A");
    m.connectPorts("controllerB", "drive_out", "motorB", "v_in", "NET_DRIVE_B");
    m.connectPorts("motorA", "tach", "controllerA", "feedback_in", "NET_TACH_A");
    m.connectPorts("motorB", "tach", "controllerB", "feedback_in", "NET_TACH_B");

    // 4. Build Mechanical Scene using the builder API
    m.scene.id = "controlled_arm_scene";
    m.scene.name = "Articulated Robotic Arm (2-DOF)";

    m.addRigidBody("arm_base", "Base Pedestal", "", { 0.0, 0.0, 0.0 });
    m.addVisualGeometry("arm_base", mechanical_scene::PrimitiveType::Cylinder,
                       { 0.25, 0.15, 0.25 }, { 0.0, 0.0, 0.0 }, juce::Colour(0xff18222b));

    m.addJoint("joint_shoulder", "Shoulder Joint", mechanical_scene::JointType::Revolute,
               "", { 0.0, 0.0, 1.0 }, true, -2.5, 2.5);
    m.bindJointSignal("joint_shoulder", "motorA.angle", 1.0, 0.0, false);
    if (auto* j = m.scene.findJoint("joint_shoulder"))
    {
        j->anchor = { 0.0, 0.1, 0.0 };
        j->binding.fallbackSignal = "arm.joint1.angle";
    }

    m.addRigidBody("upper_arm", "Upper Arm Link", "arm_base", { 0.0, 0.15, 0.0 });
    m.scene.findBody("upper_arm")->jointId = "joint_shoulder";
    m.addVisualGeometry("upper_arm", mechanical_scene::PrimitiveType::Box,
                       { 0.08, 0.5, 0.08 }, { 0.0, 0.25, 0.0 }, juce::Colour(0xff35c6ff));

    m.addJoint("joint_elbow", "Elbow Joint", mechanical_scene::JointType::Revolute,
               "", { 0.0, 0.0, 1.0 }, true, -2.8, 2.8);
    m.bindJointSignal("joint_elbow", "motorB.angle", 1.0, 0.0, false);
    if (auto* j = m.scene.findJoint("joint_elbow"))
    {
        j->anchor = { 0.0, 0.5, 0.0 };
        j->binding.fallbackSignal = "arm.joint2.angle";
    }

    m.addRigidBody("forearm", "Forearm Link & Tool", "upper_arm", { 0.0, 0.5, 0.0 });
    m.scene.findBody("forearm")->jointId = "joint_elbow";
    m.addVisualGeometry("forearm", mechanical_scene::PrimitiveType::Box,
                       { 0.06, 0.4, 0.06 }, { 0.0, 0.2, 0.0 }, juce::Colour(0xffffa630));
    m.addVisualGeometry("forearm", mechanical_scene::PrimitiveType::Pointer,
                       { 0.03, 0.1, 0.03 }, { 0.0, 0.45, 0.0 }, juce::Colour(0xffff6b6b));

    // 5. Control Panel Layout for Robot Arm
    m.controlPanel.title = "Robotic Arm Control Panel";
    m.addControlWidget({ "power_sw", sim_panel::WidgetType::ToggleSwitch, "Main Power", "system.power", "V", 0.0, 1.0, 0.0, 1.0, 0, 0, 50 });
    m.addControlWidget({ "start_btn", sim_panel::WidgetType::PushButton, "Arm Start / Home", "system.start", "V", 0.0, 1.0, 0.0, 1.0, 0, 1, 50 });
    m.addControlWidget({ "stop_btn", sim_panel::WidgetType::PushButton, "Arm Stop", "system.stop", "V", 0.0, 1.0, 0.0, 1.0, 0, 2, 50 });
    m.addControlWidget({ "estop_btn", sim_panel::WidgetType::EmergencyStop, "EMERGENCY STOP", "system.estop", "V", 0.0, 1.0, 0.0, 1.0, 0, 3, 70 });
    m.addControlWidget({ "reset_btn", sim_panel::WidgetType::PushButton, "Fault Reset", "system.reset", "V", 0.0, 1.0, 0.0, 1.0, 0, 4, 50 });

    m.addControlWidget({ "shoulder_cmd", sim_panel::WidgetType::Slider, "Shoulder Angle Cmd", "shoulder.target_angle", "deg", -90.0, 90.0, 0.0, 1.0, 0, 5, 80 });
    m.addControlWidget({ "elbow_cmd", sim_panel::WidgetType::Slider, "Elbow Angle Cmd", "elbow.target_angle", "deg", -120.0, 120.0, 0.0, 1.0, 0, 6, 80 });

    m.addControlWidget({ "shoulder_pos_gauge", sim_panel::WidgetType::AnalogGauge, "Shoulder Angle", "arm.joint1.angle", "rad", -3.14, 3.14, 0.0, 0.01, 1, 0, 100 });
    m.addControlWidget({ "elbow_pos_gauge", sim_panel::WidgetType::AnalogGauge, "Elbow Angle", "arm.joint2.angle", "rad", -3.14, 3.14, 0.0, 0.01, 1, 1, 100 });
    m.addControlWidget({ "status_lamp", sim_panel::WidgetType::IndicatorLamp, "System Ready", "system.ready", "", 0.0, 1.0, 0.0, 1.0, 1, 2, 40 });
    m.addControlWidget({ "fault_lamp", sim_panel::WidgetType::IndicatorLamp, "FAULT / E-STOP", "system.fault", "", 0.0, 1.0, 0.0, 1.0, 1, 3, 40 });

    return m;
}

} // namespace machine_builder

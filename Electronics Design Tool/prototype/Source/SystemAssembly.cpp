#include "SystemAssembly.h"
#include "FrustComponentCompiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace sim_panel
{

// -----------------------------------------------------------------------------
// SubsystemPort JSON
// -----------------------------------------------------------------------------

juce::var SubsystemPort::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("name", juce::String(name));
    obj->setProperty("direction", (int)direction);
    obj->setProperty("domain", (int)domain);
    obj->setProperty("unit", juce::String(unit));
    obj->setProperty("description", juce::String(description));
    return juce::var(obj);
}

SubsystemPort SubsystemPort::fromJson(const juce::var& json)
{
    SubsystemPort p;
    if (const auto* obj = json.getDynamicObject())
    {
        p.name = obj->getProperty("name").toString().toStdString();
        p.direction = (PortDirection)(int)obj->getProperty("direction");
        p.domain = (PortDomain)(int)obj->getProperty("domain");
        p.unit = obj->getProperty("unit").toString().toStdString();
        p.description = obj->getProperty("description").toString().toStdString();
    }
    return p;
}

// -----------------------------------------------------------------------------
// SubsystemDefinition JSON & Helpers
// -----------------------------------------------------------------------------

const SubsystemPort* SubsystemDefinition::findPort(const std::string& portName) const
{
    for (const auto& p : ports)
        if (p.name == portName)
            return &p;
    return nullptr;
}

juce::var SubsystemDefinition::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("displayName", juce::String(displayName));
    obj->setProperty("kind", (int)kind);
    obj->setProperty("description", juce::String(description));
    obj->setProperty("referenceFile", juce::String(referenceFile));
    obj->setProperty("diagramSheet", juce::String(diagramSheet));

    if (schematicData.isObject())
        obj->setProperty("schematicData", schematicData);
    if (componentData.isObject())
        obj->setProperty("componentData", componentData);

    juce::Array<juce::var> portArray;
    for (const auto& p : ports)
        portArray.add(p.toJson());
    obj->setProperty("ports", juce::var(portArray));

    auto* numObj = new juce::DynamicObject();
    for (const auto& [k, v] : defaultNumericParams)
        numObj->setProperty(juce::String(k), v);
    obj->setProperty("defaultNumericParams", juce::var(numObj));

    auto* txtObj = new juce::DynamicObject();
    for (const auto& [k, v] : defaultTextParams)
        txtObj->setProperty(juce::String(k), juce::String(v));
    obj->setProperty("defaultTextParams", juce::var(txtObj));

    return juce::var(obj);
}

SubsystemDefinition SubsystemDefinition::fromJson(const juce::var& json)
{
    SubsystemDefinition d;
    if (const auto* obj = json.getDynamicObject())
    {
        d.id = obj->getProperty("id").toString().toStdString();
        d.displayName = obj->getProperty("displayName").toString().toStdString();
        d.kind = (SubsystemKind)(int)obj->getProperty("kind");
        d.description = obj->getProperty("description").toString().toStdString();
        d.referenceFile = obj->getProperty("referenceFile").toString().toStdString();
        d.diagramSheet = obj->getProperty("diagramSheet").toString().toStdString();
        d.schematicData = obj->getProperty("schematicData");
        d.componentData = obj->getProperty("componentData");

        if (const auto* arr = obj->getProperty("ports").getArray())
            for (const auto& item : *arr)
                d.ports.push_back(SubsystemPort::fromJson(item));

        if (const auto* numObj = obj->getProperty("defaultNumericParams").getDynamicObject())
            for (const auto& p : numObj->getProperties())
                d.defaultNumericParams[p.name.toString().toStdString()] = (double)p.value;

        if (const auto* txtObj = obj->getProperty("defaultTextParams").getDynamicObject())
            for (const auto& p : txtObj->getProperties())
                d.defaultTextParams[p.name.toString().toStdString()] = p.value.toString().toStdString();
    }
    return d;
}

SubsystemDefinition SubsystemDefinition::fromSchematicJson(const juce::var& json, const std::string& defId, const std::string& name)
{
    SubsystemDefinition d;
    d.id = defId.empty() ? "user_schematic_subsystem" : defId;
    d.displayName = name.empty() ? d.id : name;
    d.kind = SubsystemKind::SchematicReference;
    d.schematicData = json;

    const auto* root = json.getDynamicObject();
    if (root == nullptr)
        return d;

    d.description = root->getProperty("description").toString().toStdString();

    const auto* comps = root->getProperty("components").getArray();
    if (comps == nullptr)
        return d;

    for (const auto& entry : *comps)
    {
        const auto* c = entry.getDynamicObject();
        if (c == nullptr)
            continue;

        const auto symbol = c->getProperty("symbol").toString();
        const auto refdes = c->getProperty("id").toString();
        const auto busName = c->getProperty("busName").toString().trim();

        if (symbol == "block_port" || symbol == "power_port" || symbol == "net_label")
        {
            const auto portName = busName.isNotEmpty() ? busName : refdes;
            if (portName.isEmpty())
                continue;

            SubsystemPort p;
            p.name = portName.toStdString();
            const auto lower = portName.toLowerCase();
            if (lower.endsWith("_in") || lower.contains("input") || lower == "load_torque" || lower == "v_in")
            {
                p.direction = PortDirection::Input;
            }
            else if (lower.endsWith("_out") || lower.contains("output") || lower == "speed" || lower == "tach" || lower == "angle")
            {
                p.direction = PortDirection::Output;
            }
            else
            {
                p.direction = PortDirection::Bidirectional;
            }

            if (lower.contains("speed") || lower.contains("torque") || lower.contains("shaft") || lower.contains("angle"))
            {
                p.domain = PortDomain::Mechanical;
                p.unit = lower.contains("speed") ? "rad/s" : lower.contains("torque") ? "N*m" : "rad";
            }
            else if (lower.contains("pwr") || lower.contains("power") || lower.contains("v_in") || lower.contains("volt"))
            {
                p.domain = PortDomain::Electrical;
                p.unit = "V";
            }
            else
            {
                p.domain = PortDomain::SignalControl;
                p.unit = "V";
            }

            if (d.findPort(p.name) == nullptr)
                d.ports.push_back(p);
        }
        else if (symbol == "dc_motor")
        {
            if (const auto* params = c->getProperty("params").getDynamicObject())
            {
                for (const auto& prop : params->getProperties())
                    d.defaultNumericParams[prop.name.toString().toStdString()] = (double)prop.value;
            }
        }
        else if (symbol == "resistor" || symbol == "capacitor" || symbol == "inductor")
        {
            const auto valStr = c->getProperty("value").toString();
            double numVal = 0.0;
            if (circuit_sim::parseValue(valStr.toStdString(), numVal))
                d.defaultNumericParams[refdes.toStdString()] = numVal;
        }
    }

    return d;
}

bool SubsystemDefinition::loadSchematicDefinition(const juce::File& file, SubsystemDefinition& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Schematic file not found: " + file.getFullPathName();
        return false;
    }
    const auto parsed = juce::JSON::parse(file);
    if (!parsed.isObject())
    {
        error = "Invalid JSON in schematic file: " + file.getFullPathName();
        return false;
    }
    juce::var circuitData = parsed;
    if (const auto* obj = parsed.getDynamicObject())
    {
        if (obj->hasProperty("circuit"))
            circuitData = obj->getProperty("circuit");
    }

    const auto defId = file.getFileNameWithoutExtension().toStdString();
    out = fromSchematicJson(circuitData, defId, file.getFileNameWithoutExtension().toStdString());
    out.referenceFile = file.getFullPathName().toStdString();
    return true;
}

SubsystemDefinition SubsystemDefinition::fromFrustComponent(const frust_component::Definition& def, const std::string& defId)
{
    SubsystemDefinition d;
    d.id = defId.empty() ? def.name.toStdString() : defId;
    d.displayName = def.name.toStdString();
    d.kind = SubsystemKind::ProgrammableComponent;
    d.description = def.description.toStdString();
    d.componentData = frust_component::toVar(def);

    for (const auto& pin : def.pins)
    {
        SubsystemPort p;
        p.name = pin.id.toStdString();
        p.direction = (pin.role == frust_component::PinRole::Input) ? PortDirection::Input : PortDirection::Output;
        p.domain = PortDomain::SignalControl;
        p.unit = "V";
        p.description = ("Pin " + pin.id).toStdString();
        d.ports.push_back(p);
    }

    for (const auto& param : def.parameters)
        d.defaultNumericParams[param.name.toStdString()] = param.defaultValue;

    return d;
}

bool SubsystemDefinition::loadFrustComponentDefinition(const juce::File& file, SubsystemDefinition& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Component definition file not found: " + file.getFullPathName();
        return false;
    }
    frust_component::Definition definition;
    if (!frust_component::load(file, definition, error))
        return false;

    out = fromFrustComponent(definition, file.getFileNameWithoutExtension().toStdString());
    out.referenceFile = file.getFullPathName().toStdString();
    return true;
}

// -----------------------------------------------------------------------------
// SubsystemInstance JSON
// -----------------------------------------------------------------------------

juce::var SubsystemInstance::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("instanceId", juce::String(instanceId));
    obj->setProperty("definitionId", juce::String(definitionId));
    obj->setProperty("title", juce::String(title));
    obj->setProperty("canvasX", canvasX);
    obj->setProperty("canvasY", canvasY);

    auto* numObj = new juce::DynamicObject();
    for (const auto& [k, v] : numericParams)
        numObj->setProperty(juce::String(k), v);
    obj->setProperty("numericParams", juce::var(numObj));

    auto* txtObj = new juce::DynamicObject();
    for (const auto& [k, v] : textParams)
        txtObj->setProperty(juce::String(k), juce::String(v));
    obj->setProperty("textParams", juce::var(txtObj));

    return juce::var(obj);
}

SubsystemInstance SubsystemInstance::fromJson(const juce::var& json)
{
    SubsystemInstance inst;
    if (const auto* obj = json.getDynamicObject())
    {
        inst.instanceId = obj->getProperty("instanceId").toString().toStdString();
        inst.definitionId = obj->getProperty("definitionId").toString().toStdString();
        inst.title = obj->getProperty("title").toString().toStdString();
        inst.canvasX = obj->hasProperty("canvasX") ? (double)obj->getProperty("canvasX") : 120.0;
        inst.canvasY = obj->hasProperty("canvasY") ? (double)obj->getProperty("canvasY") : 120.0;

        if (const auto* numObj = obj->getProperty("numericParams").getDynamicObject())
            for (const auto& p : numObj->getProperties())
                inst.numericParams[p.name.toString().toStdString()] = (double)p.value;

        if (const auto* txtObj = obj->getProperty("textParams").getDynamicObject())
            for (const auto& p : txtObj->getProperties())
                inst.textParams[p.name.toString().toStdString()] = p.value.toString().toStdString();
    }
    return inst;
}

// -----------------------------------------------------------------------------
// SignalConnection JSON
// -----------------------------------------------------------------------------

juce::var SignalConnection::toJson() const
{
    auto* obj = new juce::DynamicObject();
    const auto stableId = connectionId.empty()
        ? fromInstance + "." + fromPort + "__to__" + toInstance + "." + toPort
        : connectionId;
    obj->setProperty("connectionId", juce::String(stableId));
    obj->setProperty("fromInstance", juce::String(fromInstance));
    obj->setProperty("fromPort", juce::String(fromPort));
    obj->setProperty("toInstance", juce::String(toInstance));
    obj->setProperty("toPort", juce::String(toPort));
    obj->setProperty("netName", juce::String(netName));
    juce::Array<juce::var> points;
    for (const auto& p : routePoints)
    {
        auto* point = new juce::DynamicObject();
        point->setProperty("x", p.x);
        point->setProperty("y", p.y);
        points.add(juce::var(point));
    }
    obj->setProperty("routePoints", juce::var(points));
    return juce::var(obj);
}

SignalConnection SignalConnection::fromJson(const juce::var& json)
{
    SignalConnection conn;
    if (const auto* obj = json.getDynamicObject())
    {
        conn.fromInstance = obj->getProperty("fromInstance").toString().toStdString();
        conn.fromPort = obj->getProperty("fromPort").toString().toStdString();
        conn.toInstance = obj->getProperty("toInstance").toString().toStdString();
        conn.toPort = obj->getProperty("toPort").toString().toStdString();
        conn.netName = obj->getProperty("netName").toString().toStdString();
        conn.connectionId = obj->getProperty("connectionId").toString().toStdString();
        if (conn.connectionId.empty())
            conn.connectionId = conn.fromInstance + "." + conn.fromPort + "__to__" + conn.toInstance + "." + conn.toPort;
        if (const auto* points = obj->getProperty("routePoints").getArray())
        {
            for (const auto& item : *points)
            {
                if (const auto* point = item.getDynamicObject())
                    conn.routePoints.push_back({ (double)point->getProperty("x"), (double)point->getProperty("y") });
            }
        }
    }
    return conn;
}

// -----------------------------------------------------------------------------
// SystemAssembly Queries & Built-in Definitions
// -----------------------------------------------------------------------------

const SubsystemDefinition* SystemAssembly::findDefinition(const std::string& defId) const
{
    for (const auto& def : definitions)
        if (def.id == defId)
            return &def;
    return nullptr;
}

const SubsystemInstance* SystemAssembly::findInstance(const std::string& instId) const
{
    for (const auto& inst : instances)
        if (inst.instanceId == instId)
            return &inst;
    return nullptr;
}

SubsystemDefinition SystemAssembly::makeDCMotorDefinition()
{
    SubsystemDefinition d;
    d.id = "dc_motor";
    d.displayName = "DC Motor Actuator Subsystem";
    d.kind = SubsystemKind::DCMotor;
    d.description = "Permanent magnet brushed DC motor with rotor inertia, back-EMF, and viscous damping.";

    d.ports = {
        { "v_in", PortDirection::Input, PortDomain::Electrical, "V", "Armature supply voltage positive terminal" },
        { "gnd", PortDirection::Input, PortDomain::Electrical, "V", "Armature ground return" },
        { "load_torque", PortDirection::Input, PortDomain::Mechanical, "N*m", "Shaft mechanical load torque disturbance" },
        { "speed", PortDirection::Output, PortDomain::Mechanical, "rad/s", "Shaft angular velocity" },
        { "angle", PortDirection::Output, PortDomain::Mechanical, "rad", "Shaft angular position" },
        { "tach", PortDirection::Output, PortDomain::Electrical, "V", "Tachometer feedback signal (10 mV per rad/s)" },
        { "current", PortDirection::Output, PortDomain::Electrical, "A", "Armature current" }
    };

    d.defaultNumericParams = {
        { "ra", 2.0 },
        { "la", 0.001 },
        { "kt", 0.02 },
        { "ke", 0.02 },
        { "j", 0.00001 },
        { "b", 0.000001 }
    };

    return d;
}

SubsystemDefinition SystemAssembly::makeMotorControllerDefinition()
{
    SubsystemDefinition d;
    d.id = "motor_speed_controller";
    d.displayName = "Closed-Loop Motor Speed Controller Subsystem";
    d.kind = SubsystemKind::MotorSpeedController;
    d.description = "Programmable PID velocity regulator with integral anti-windup, E-stop latch, and lamp drivers.";

    d.ports = {
        { "pwr_in", PortDirection::Input, PortDomain::SignalControl, "V", "Main system power sense (>2V = ON)" },
        { "start_in", PortDirection::Input, PortDomain::SignalControl, "V", "Start command pulse (>2V = active)" },
        { "stop_in", PortDirection::Input, PortDomain::SignalControl, "V", "Stop command pulse (>2V = active)" },
        { "estop_in", PortDirection::Input, PortDomain::SignalControl, "V", "Emergency stop sense (>2V = E-STOP ACTIVE)" },
        { "reset_in", PortDirection::Input, PortDomain::SignalControl, "V", "Fault reset command (>2V = reset)" },
        { "setpoint_in", PortDirection::Input, PortDomain::SignalControl, "V", "Target velocity (10 mV per rad/s)" },
        { "feedback_in", PortDirection::Input, PortDomain::SignalControl, "V", "Tachometer feedback (10 mV per rad/s)" },
        { "drive_out", PortDirection::Output, PortDomain::Electrical, "V", "Actuator output drive voltage" },
        { "run_lamp", PortDirection::Output, PortDomain::SignalControl, "V", "Motor running status indicator (5V = ON)" },
        { "fault_lamp", PortDirection::Output, PortDomain::SignalControl, "V", "Fault / E-Stop trip indicator (5V = ON)" }
    };

    d.defaultNumericParams = {
        { "kp", 0.08 },
        { "ki", 2.0 },
        { "kd", 0.0002 },
        { "v_max", 18.0 },
        { "v_min", 0.0 }
    };

    return d;
}

SubsystemDefinition SystemAssembly::makeCoordinatedSequencerDefinition()
{
    SubsystemDefinition d;
    d.id = "coordinated_sequencer";
    d.displayName = "System Coordinator & Automation Sequencer";
    d.kind = SubsystemKind::CoordinatedSequencer;
    d.description = "Coordinates multi-actuator power-up, delayed sequencing, interlocks, and master E-stop.";

    d.ports = {
        { "master_power", PortDirection::Input, PortDomain::SignalControl, "V", "Master system power switch" },
        { "master_start", PortDirection::Input, PortDomain::SignalControl, "V", "Master system start button" },
        { "master_stop", PortDirection::Input, PortDomain::SignalControl, "V", "Master system stop button" },
        { "master_estop", PortDirection::Input, PortDomain::SignalControl, "V", "Master emergency stop mushroom" },
        { "master_reset", PortDirection::Input, PortDomain::SignalControl, "V", "Master fault reset button" },
        { "start_a_out", PortDirection::Output, PortDomain::SignalControl, "V", "Subsystem A start command" },
        { "start_b_out", PortDirection::Output, PortDomain::SignalControl, "V", "Subsystem B start command (delayed)" },
        { "stop_all_out", PortDirection::Output, PortDomain::SignalControl, "V", "Stop command to all subsystems" },
        { "estop_all_out", PortDirection::Output, PortDomain::SignalControl, "V", "E-stop line to all subsystems" },
        { "system_ready", PortDirection::Output, PortDomain::SignalControl, "V", "System ready indicator" },
        { "system_fault", PortDirection::Output, PortDomain::SignalControl, "V", "System fault indicator" }
    };

    d.defaultNumericParams = {
        { "motor_b_delay_sec", 0.15 } // 150 ms delayed start for Motor B
    };

    return d;
}

// -----------------------------------------------------------------------------
// Factory: Dual-Motor Coordinated System Assembly
// -----------------------------------------------------------------------------

SystemAssembly SystemAssembly::createDualMotorSystem()
{
    SystemAssembly a;
    a.name = "Dual-Motor Coordinated Automation System";
    a.description = "Coordinated dual-motor system with independent closed-loop velocity controllers, "
                    "delayed start sequencing, load disturbance isolation, and master emergency stop.";
    a.version = "1.0";

    // 1. Add Definitions
    a.definitions.push_back(makeDCMotorDefinition());
    a.definitions.push_back(makeMotorControllerDefinition());
    a.definitions.push_back(makeCoordinatedSequencerDefinition());

    // 2. Add Instances
    // Motor A
    SubsystemInstance mA;
    mA.instanceId = "motorA";
    mA.definitionId = "dc_motor";
    mA.title = "Drive Motor A (Primary)";
    mA.numericParams = { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00001 }, { "b", 0.000001 } };
    a.instances.push_back(mA);

    // Motor B
    SubsystemInstance mB;
    mB.instanceId = "motorB";
    mB.definitionId = "dc_motor";
    mB.title = "Drive Motor B (Secondary)";
    mB.numericParams = { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00001 }, { "b", 0.000001 } };
    a.instances.push_back(mB);

    // Controller A
    SubsystemInstance ctlA;
    ctlA.instanceId = "controllerA";
    ctlA.definitionId = "motor_speed_controller";
    ctlA.title = "Speed Controller A";
    ctlA.numericParams = { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } };
    a.instances.push_back(ctlA);

    // Controller B
    SubsystemInstance ctlB;
    ctlB.instanceId = "controllerB";
    ctlB.definitionId = "motor_speed_controller";
    ctlB.title = "Speed Controller B";
    ctlB.numericParams = { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } };
    a.instances.push_back(ctlB);

    // Sequencer
    SubsystemInstance seq;
    seq.instanceId = "sequencer";
    seq.definitionId = "coordinated_sequencer";
    seq.title = "Coordinated Startup Sequencer";
    seq.numericParams = { { "motor_b_delay_sec", 0.15 } };
    a.instances.push_back(seq);

    // 3. Connect Subsystems
    // Sequencer start outputs -> Controller start inputs
    a.connections.push_back({ "sequencer", "start_a_out", "controllerA", "start_in", "NET_START_A" });
    a.connections.push_back({ "sequencer", "start_b_out", "controllerB", "start_in", "NET_START_B" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerA", "stop_in", "NET_STOP_A" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerB", "stop_in", "NET_STOP_B" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerA", "estop_in", "NET_ESTOP" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerB", "estop_in", "NET_ESTOP" });

    // Controller Drives -> Motor Voltage Inputs
    a.connections.push_back({ "controllerA", "drive_out", "motorA", "v_in", "NET_DRIVE_A" });
    a.connections.push_back({ "controllerB", "drive_out", "motorB", "v_in", "NET_DRIVE_B" });

    // Motor Tachometer Feedback -> Controller Feedback Inputs
    a.connections.push_back({ "motorA", "tach", "controllerA", "feedback_in", "NET_TACH_A" });
    a.connections.push_back({ "motorB", "tach", "controllerB", "feedback_in", "NET_TACH_B" });

    // 4. Exposed System-Level Ports / Signals for Controls
    a.systemPorts = {
        { "power_switch", PortDirection::Input, PortDomain::SignalControl, "V", "Master Power Switch" },
        { "start_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Start Button" },
        { "stop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Stop Button" },
        { "estop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Emergency Stop Button" },
        { "reset_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Fault Reset Button" },
        { "motorA.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Motor A Speed Setpoint" },
        { "motorB.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Motor B Speed Setpoint" },
        { "motorA.load_torque", PortDirection::Input, PortDomain::Mechanical, "N*m", "Motor A Load Torque" },
        { "motorB.load_torque", PortDirection::Input, PortDomain::Mechanical, "N*m", "Motor B Load Torque" }
    };

    // Attach P6 Mechanical Scene
    a.mechanicalScene = mechanical_scene::MechanicalScene::createDualMotorShaftScene();

    return a;
}

// -----------------------------------------------------------------------------
// Factory: Controlled Robotic Arm System (P6 Demonstration B)
// -----------------------------------------------------------------------------

SystemAssembly SystemAssembly::createControlledArmSystem()
{
    SystemAssembly a;
    a.name = "Controlled Articulated Robotic Arm (2-DOF)";
    a.description = "Two-link articulated mechanical arm driven by closed-loop position controllers with joint limits.";
    a.version = "1.0";

    // Reusable Subsystem Definitions
    SubsystemDefinition motorDef = makeDCMotorDefinition();
    motorDef.id = "arm_motor_module";
    a.definitions.push_back(motorDef);

    SubsystemDefinition ctlDef = makeMotorControllerDefinition();
    ctlDef.id = "arm_joint_controller";
    a.definitions.push_back(ctlDef);

    SubsystemDefinition seqDef = makeCoordinatedSequencerDefinition();
    a.definitions.push_back(seqDef);

    // Instances
    SubsystemInstance m1;
    m1.instanceId = "motorA";
    m1.definitionId = "arm_motor_module";
    m1.title = "Shoulder Joint Actuator";
    m1.numericParams = { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00005 }, { "b", 0.00001 } };
    a.instances.push_back(m1);

    SubsystemInstance m2;
    m2.instanceId = "motorB";
    m2.definitionId = "arm_motor_module";
    m2.title = "Elbow Joint Actuator";
    m2.numericParams = { { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 }, { "ke", 0.02 }, { "j", 0.00002 }, { "b", 0.00001 } };
    a.instances.push_back(m2);

    SubsystemInstance c1;
    c1.instanceId = "controllerA";
    c1.definitionId = "arm_joint_controller";
    c1.title = "Shoulder Controller";
    c1.numericParams = { { "kp", 0.1 }, { "ki", 1.5 }, { "kd", 0.0005 }, { "v_max", 12.0 }, { "v_min", 0.0 } };
    a.instances.push_back(c1);

    SubsystemInstance c2;
    c2.instanceId = "controllerB";
    c2.definitionId = "arm_joint_controller";
    c2.title = "Elbow Controller";
    c2.numericParams = { { "kp", 0.1 }, { "ki", 1.5 }, { "kd", 0.0005 }, { "v_max", 12.0 }, { "v_min", 0.0 } };
    a.instances.push_back(c2);

    SubsystemInstance seq;
    seq.instanceId = "sequencer";
    seq.definitionId = "coordinated_sequencer";
    seq.title = "Arm Startup & Homing Sequencer";
    seq.numericParams = { { "motor_b_delay_sec", 0.1 } };
    a.instances.push_back(seq);

    // Connections
    a.connections.push_back({ "sequencer", "start_a_out", "controllerA", "start_in", "NET_START_A" });
    a.connections.push_back({ "sequencer", "start_b_out", "controllerB", "start_in", "NET_START_B" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerA", "stop_in", "NET_STOP_A" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerB", "stop_in", "NET_STOP_B" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerA", "estop_in", "NET_ESTOP" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerB", "estop_in", "NET_ESTOP" });

    a.connections.push_back({ "controllerA", "drive_out", "motorA", "v_in", "NET_DRIVE_A" });
    a.connections.push_back({ "controllerB", "drive_out", "motorB", "v_in", "NET_DRIVE_B" });
    a.connections.push_back({ "motorA", "tach", "controllerA", "feedback_in", "NET_TACH_A" });
    a.connections.push_back({ "motorB", "tach", "controllerB", "feedback_in", "NET_TACH_B" });

    // System Ports
    a.systemPorts = {
        { "power_switch", PortDirection::Input, PortDomain::SignalControl, "V", "Arm Master Power" },
        { "start_button", PortDirection::Input, PortDomain::SignalControl, "V", "Arm Start / Home Sequence" },
        { "stop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Arm Stop" },
        { "estop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Arm Emergency Stop" },
        { "reset_button", PortDirection::Input, PortDomain::SignalControl, "V", "Arm Fault Reset" },
        { "motorA.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Joint 1 Target" },
        { "motorB.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Joint 2 Target" }
    };

    // Attach P6 Mechanical Scene for Robotic Arm
    a.mechanicalScene = mechanical_scene::MechanicalScene::createControlledArmScene();

    return a;
}

// -----------------------------------------------------------------------------
// Factory: User-Assembled Motor Control System (P5 Demonstration)
// -----------------------------------------------------------------------------

SystemAssembly SystemAssembly::createUserAssembledMotorSystem()
{
    SystemAssembly a;
    a.name = "User-Assembled Dual-Motor Coordinated System";
    a.description = "User-assembled system built from a reusable motor-drive schematic diagram, "
                    "a reusable Node Designer / FRust programmable controller, and a coordinated sequencer.";
    a.version = "1.0";

    // 1. Reusable Motor Drive Schematic Diagram JSON (schemaVersion 2, electronics_circuit)
    auto* schematicRoot = new juce::DynamicObject();
    schematicRoot->setProperty("schemaVersion", 2);
    schematicRoot->setProperty("kind", "electronics_circuit");
    schematicRoot->setProperty("name", "Reusable Motor Drive Schematic Subsystem");
    schematicRoot->setProperty("description", "Electromechanical motor drive subsystem with DC motor, tachometer, and mechanical shaft ports.");

    juce::Array<juce::var> components;

    // Interface Port: v_in (Electrical Voltage Drive Input)
    auto* pVin = new juce::DynamicObject();
    pVin->setProperty("id", "PORT_VIN");
    pVin->setProperty("symbol", "block_port");
    pVin->setProperty("busName", "v_in");
    auto* pVinPins = new juce::DynamicObject();
    pVinPins->setProperty("1", "NET_VIN");
    pVin->setProperty("pins", juce::var(pVinPins));
    components.add(juce::var(pVin));

    // Interface Port: load_torque (Mechanical Load Torque Input)
    auto* pLoad = new juce::DynamicObject();
    pLoad->setProperty("id", "PORT_LOAD");
    pLoad->setProperty("symbol", "block_port");
    pLoad->setProperty("busName", "load_torque");
    auto* pLoadPins = new juce::DynamicObject();
    pLoadPins->setProperty("1", "NET_SHAFT");
    pLoad->setProperty("pins", juce::var(pLoadPins));
    components.add(juce::var(pLoad));

    // Interface Port: speed (Mechanical Velocity Output)
    auto* pSpeed = new juce::DynamicObject();
    pSpeed->setProperty("id", "PORT_SPEED");
    pSpeed->setProperty("symbol", "block_port");
    pSpeed->setProperty("busName", "speed");
    auto* pSpeedPins = new juce::DynamicObject();
    pSpeedPins->setProperty("1", "NET_SHAFT");
    pSpeed->setProperty("pins", juce::var(pSpeedPins));
    components.add(juce::var(pSpeed));

    // Interface Port: tach (Electrical Tachometer Feedback Output)
    auto* pTach = new juce::DynamicObject();
    pTach->setProperty("id", "PORT_TACH");
    pTach->setProperty("symbol", "block_port");
    pTach->setProperty("busName", "tach");
    auto* pTachPins = new juce::DynamicObject();
    pTachPins->setProperty("1", "NET_TACH");
    pTach->setProperty("pins", juce::var(pTachPins));
    components.add(juce::var(pTach));

    // DC Motor component M1
    auto* pMotor = new juce::DynamicObject();
    pMotor->setProperty("id", "M1");
    pMotor->setProperty("symbol", "dc_motor");
    pMotor->setProperty("value", "generic_dc_motor");
    auto* mParams = new juce::DynamicObject();
    mParams->setProperty("ra", 2.0);
    mParams->setProperty("la", 0.001);
    mParams->setProperty("kt", 0.02);
    mParams->setProperty("ke", 0.02);
    mParams->setProperty("j", 0.00001);
    mParams->setProperty("b", 0.000001);
    pMotor->setProperty("params", juce::var(mParams));
    auto* mPins = new juce::DynamicObject();
    mPins->setProperty("pos", "NET_VIN");
    mPins->setProperty("neg", "0");
    mPins->setProperty("shaft", "NET_SHAFT");
    mPins->setProperty("angle", "NET_ANGLE");
    pMotor->setProperty("pins", juce::var(mPins));
    components.add(juce::var(pMotor));

    // Tachometer TG1
    auto* pTachComp = new juce::DynamicObject();
    pTachComp->setProperty("id", "TG1");
    pTachComp->setProperty("symbol", "tachometer");
    auto* tgPins = new juce::DynamicObject();
    tgPins->setProperty("shaft", "NET_SHAFT");
    tgPins->setProperty("out", "NET_TACH");
    pTachComp->setProperty("pins", juce::var(tgPins));
    components.add(juce::var(pTachComp));

    // Mechanical load torque current source TL1
    auto* pLoadComp = new juce::DynamicObject();
    pLoadComp->setProperty("id", "TL1");
    pLoadComp->setProperty("symbol", "current_source");
    auto* tlPins = new juce::DynamicObject();
    tlPins->setProperty("1", "NET_SHAFT");
    tlPins->setProperty("2", "0");
    pLoadComp->setProperty("pins", juce::var(tlPins));
    components.add(juce::var(pLoadComp));

    schematicRoot->setProperty("components", components);
    juce::var schematicVar(schematicRoot);

    auto motorDriveDef = SubsystemDefinition::fromSchematicJson(schematicVar, "user_motor_drive", "User Motor Drive (Schematic)");
    a.definitions.push_back(motorDriveDef);

    // 2. Reusable Programmable Controller Definition (Node Designer / FRust Component)
    frust_component::Definition ctlComp;
    ctlComp.name = "user_motor_controller";
    ctlComp.description = "FRust Programmable Closed-Loop Velocity Controller with Anti-Windup & E-Stop Interlock";
    ctlComp.pins = {
        { "pwr_in", frust_component::PinRole::Input },
        { "start_in", frust_component::PinRole::Input },
        { "stop_in", frust_component::PinRole::Input },
        { "estop_in", frust_component::PinRole::Input },
        { "reset_in", frust_component::PinRole::Input },
        { "setpoint_in", frust_component::PinRole::Input },
        { "feedback_in", frust_component::PinRole::Input },
        { "drive_out", frust_component::PinRole::VoltageOutput },
        { "run_lamp", frust_component::PinRole::VoltageOutput },
        { "fault_lamp", frust_component::PinRole::VoltageOutput }
    };
    ctlComp.parameters = {
        { "kp", 0.08 },
        { "ki", 2.0 },
        { "kd", 0.0002 },
        { "v_max", 18.0 },
        { "v_min", 0.0 }
    };
    ctlComp.state = {
        { "state", 0.0 },
        { "integral", 0.0 },
        { "last_error", 0.0 },
        { "last_t", 0.0 }
    };
    ctlComp.programFile = "user_motor_controller.frnode.json";

    auto controllerDef = SubsystemDefinition::fromFrustComponent(ctlComp, "user_motor_controller");
    a.definitions.push_back(controllerDef);

    // 3. Coordinated Sequencer Subsystem Definition
    a.definitions.push_back(makeCoordinatedSequencerDefinition());

    // 4. Instances
    SubsystemInstance drvA;
    drvA.instanceId = "motorDriveA";
    drvA.definitionId = "user_motor_drive";
    drvA.title = "Drive A (Schematic Subsystem)";
    a.instances.push_back(drvA);

    SubsystemInstance drvB;
    drvB.instanceId = "motorDriveB";
    drvB.definitionId = "user_motor_drive";
    drvB.title = "Drive B (Schematic Subsystem)";
    a.instances.push_back(drvB);

    SubsystemInstance cA;
    cA.instanceId = "controllerA";
    cA.definitionId = "user_motor_controller";
    cA.title = "Controller A (FRust Programmable Component)";
    cA.numericParams = { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } };
    a.instances.push_back(cA);

    SubsystemInstance cB;
    cB.instanceId = "controllerB";
    cB.definitionId = "user_motor_controller";
    cB.title = "Controller B (FRust Programmable Component)";
    cB.numericParams = { { "kp", 0.08 }, { "ki", 2.0 }, { "kd", 0.0002 }, { "v_max", 18.0 }, { "v_min", 0.0 } };
    a.instances.push_back(cB);

    SubsystemInstance seq;
    seq.instanceId = "sequencer";
    seq.definitionId = "coordinated_sequencer";
    seq.title = "Coordinated Startup Sequencer";
    seq.numericParams = { { "motor_b_delay_sec", 0.15 } };
    a.instances.push_back(seq);

    // 5. Connections
    a.connections.push_back({ "sequencer", "start_a_out", "controllerA", "start_in", "NET_START_A" });
    a.connections.push_back({ "sequencer", "start_b_out", "controllerB", "start_in", "NET_START_B" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerA", "stop_in", "NET_STOP_A" });
    a.connections.push_back({ "sequencer", "stop_all_out", "controllerB", "stop_in", "NET_STOP_B" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerA", "estop_in", "NET_ESTOP" });
    a.connections.push_back({ "sequencer", "estop_all_out", "controllerB", "estop_in", "NET_ESTOP" });

    a.connections.push_back({ "controllerA", "drive_out", "motorDriveA", "v_in", "NET_DRIVE_A" });
    a.connections.push_back({ "controllerB", "drive_out", "motorDriveB", "v_in", "NET_DRIVE_B" });

    a.connections.push_back({ "motorDriveA", "tach", "controllerA", "feedback_in", "NET_TACH_A" });
    a.connections.push_back({ "motorDriveB", "tach", "controllerB", "feedback_in", "NET_TACH_B" });

    // 6. System-Level Exposed Ports
    a.systemPorts = {
        { "power_switch", PortDirection::Input, PortDomain::SignalControl, "V", "Master Power Switch" },
        { "start_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Start Button" },
        { "stop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Stop Button" },
        { "estop_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Emergency Stop Button" },
        { "reset_button", PortDirection::Input, PortDomain::SignalControl, "V", "Master Fault Reset Button" },
        { "motorDriveA.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Motor Drive A Speed Setpoint" },
        { "motorDriveB.speed_setpoint", PortDirection::Input, PortDomain::SignalControl, "rad/s", "Motor Drive B Speed Setpoint" },
        { "motorDriveA.load_torque", PortDirection::Input, PortDomain::Mechanical, "N*m", "Motor Drive A Load Torque" },
        { "motorDriveB.load_torque", PortDirection::Input, PortDomain::Mechanical, "N*m", "Motor Drive B Load Torque" }
    };

    // Attach P6 Mechanical Scene
    a.mechanicalScene = mechanical_scene::MechanicalScene::createDualMotorShaftScene();

    return a;
}

namespace
{
juce::File resolveReferenceFile(const std::string& referenceFile, const juce::File& projectFolder);

const SubsystemPort* findSystemPort(const SystemAssembly& assembly, const std::string& portName)
{
    for (const auto& p : assembly.systemPorts)
        if (p.name == portName)
            return &p;
    return nullptr;
}

bool canSourceFrom(const SubsystemPort& port, bool isSystemPort)
{
    if (isSystemPort)
        return port.direction == PortDirection::Input || port.direction == PortDirection::Bidirectional;
    return port.direction == PortDirection::Output || port.direction == PortDirection::Bidirectional;
}

bool canConnectTo(const SubsystemPort& port, bool isSystemPort)
{
    if (isSystemPort)
        return port.direction == PortDirection::Output || port.direction == PortDirection::Bidirectional;
    return port.direction == PortDirection::Input || port.direction == PortDirection::Bidirectional;
}

bool domainsCompatible(PortDomain a, PortDomain b)
{
    if (a == b)
        return true;
    const auto electricalLike = [](PortDomain d) {
        return d == PortDomain::Electrical || d == PortDomain::SignalControl;
    };
    return electricalLike(a) && electricalLike(b);
}
}

std::vector<std::string> SystemAssembly::validate(const juce::File& projectFolder) const
{
    std::vector<std::string> issues;
    std::set<std::string> instIds;

    for (const auto& inst : instances)
    {
        if (inst.instanceId.empty())
            issues.push_back("Instance has empty instanceId.");
        else if (!instIds.insert(inst.instanceId).second)
            issues.push_back("Duplicate instance identifier: " + inst.instanceId);

        const auto* def = findDefinition(inst.definitionId);
        if (!def)
        {
            issues.push_back("Instance '" + inst.instanceId + "' references unknown definitionId: " + inst.definitionId);
        }
        else
        {
            if (!def->referenceFile.empty()
                && (projectFolder.exists() || juce::File::isAbsolutePath(juce::String(def->referenceFile))))
            {
                juce::File refFile = resolveReferenceFile(def->referenceFile, projectFolder);
                if (!refFile.existsAsFile())
                {
                    issues.push_back("Subsystem definition '" + def->id + "' reference file not found: " + def->referenceFile);
                }
            }
        }
    }

    for (const auto& conn : connections)
    {
        const auto* srcInst = conn.fromInstance == "system" ? nullptr : findInstance(conn.fromInstance);
        const auto* dstInst = conn.toInstance == "system" ? nullptr : findInstance(conn.toInstance);
        const SubsystemPort* srcPort = nullptr;
        const SubsystemPort* dstPort = nullptr;

        if (conn.fromInstance == "system")
        {
            srcPort = findSystemPort(*this, conn.fromPort);
            if (srcPort == nullptr)
                issues.push_back("Connection references non-existent source system port: system." + conn.fromPort);
        }
        else if (!srcInst)
            issues.push_back("Connection references non-existent source instance: " + conn.fromInstance);

        if (conn.toInstance == "system")
        {
            dstPort = findSystemPort(*this, conn.toPort);
            if (dstPort == nullptr)
                issues.push_back("Connection references non-existent destination system port: system." + conn.toPort);
        }
        else if (!dstInst)
            issues.push_back("Connection references non-existent destination instance: " + conn.toInstance);

        if (srcInst)
        {
            const auto* srcDef = findDefinition(srcInst->definitionId);
            if (srcDef)
            {
                srcPort = srcDef->findPort(conn.fromPort);
                if (srcPort == nullptr)
                    issues.push_back("Port '" + conn.fromPort + "' not found on source instance '" + conn.fromInstance + "' (" + srcDef->id + ")");
            }
        }
        if (dstInst)
        {
            const auto* dstDef = findDefinition(dstInst->definitionId);
            if (dstDef)
            {
                dstPort = dstDef->findPort(conn.toPort);
                if (dstPort == nullptr)
                    issues.push_back("Port '" + conn.toPort + "' not found on destination instance '" + conn.toInstance + "' (" + dstDef->id + ")");
            }
        }

        if (srcPort != nullptr && !canSourceFrom(*srcPort, conn.fromInstance == "system"))
        {
            issues.push_back("Port '" + conn.sourcePath() + "' cannot drive a connection.");
        }
        if (dstPort != nullptr && !canConnectTo(*dstPort, conn.toInstance == "system"))
        {
            issues.push_back("Port '" + conn.destPath() + "' cannot receive a connection.");
        }
        if (srcPort != nullptr && dstPort != nullptr && !domainsCompatible(srcPort->domain, dstPort->domain))
        {
            issues.push_back("Connection '" + conn.sourcePath() + " -> " + conn.destPath() + "' has incompatible signal domains.");
        }
    }

    return issues;
}

std::vector<std::string> SystemAssembly::discoverSignals() const
{
    std::vector<std::string> sigs;
    sigs.push_back("system.power");
    sigs.push_back("system.start");
    sigs.push_back("system.stop");
    sigs.push_back("system.estop");
    sigs.push_back("system.reset");
    sigs.push_back("system.ready");
    sigs.push_back("system.fault");

    for (const auto& inst : instances)
    {
        const auto* def = findDefinition(inst.definitionId);
        if (!def) continue;

        for (const auto& p : def->ports)
        {
            sigs.push_back(inst.instanceId + "." + p.name);
        }

        if (def->kind == SubsystemKind::DCMotor || def->id.find("motor") != std::string::npos)
        {
            sigs.push_back(inst.instanceId + ".speed");
            sigs.push_back(inst.instanceId + ".current");
            sigs.push_back(inst.instanceId + ".angle");
            sigs.push_back(inst.instanceId + ".voltage");
            sigs.push_back(inst.instanceId + ".load_torque");
            if (inst.instanceId == "motorA")
            {
                sigs.push_back("load_torque");
            }
        }
        if (def->kind == SubsystemKind::MotorSpeedController || def->id.find("controller") != std::string::npos)
        {
            sigs.push_back(inst.instanceId + ".drive");
            sigs.push_back(inst.instanceId + ".setpoint");
            sigs.push_back(inst.instanceId + ".speed_setpoint");
            sigs.push_back(inst.instanceId + ".running");
            sigs.push_back(inst.instanceId + ".fault");
            if (inst.instanceId == "controllerA")
            {
                sigs.push_back("motorA.speed_setpoint");
                sigs.push_back("speed_setpoint");
            }
            else if (inst.instanceId == "controllerB")
            {
                sigs.push_back("motorB.speed_setpoint");
            }
        }
    }

    for (const auto& sp : systemPorts)
        sigs.push_back("system." + sp.name);

    return sigs;
}

// -----------------------------------------------------------------------------
// SystemAssembly JSON & File I/O
// -----------------------------------------------------------------------------

juce::var SystemAssembly::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("name", juce::String(name));
    obj->setProperty("description", juce::String(description));
    obj->setProperty("version", juce::String(version));

    juce::Array<juce::var> defs;
    for (const auto& d : definitions)
        defs.add(d.toJson());
    obj->setProperty("definitions", juce::var(defs));

    juce::Array<juce::var> insts;
    for (const auto& inst : instances)
        insts.add(inst.toJson());
    obj->setProperty("instances", juce::var(insts));

    juce::Array<juce::var> conns;
    for (const auto& c : connections)
        conns.add(c.toJson());
    obj->setProperty("connections", juce::var(conns));

    juce::Array<juce::var> ports;
    for (const auto& p : systemPorts)
        ports.add(p.toJson());
    obj->setProperty("systemPorts", juce::var(ports));

    if (!mechanicalScene.bodies.empty() || !mechanicalScene.joints.empty())
        obj->setProperty("mechanicalScene", mechanicalScene.toJson());

    return juce::var(obj);
}

SystemAssembly SystemAssembly::fromJson(const juce::var& json)
{
    SystemAssembly a;
    if (const auto* obj = json.getDynamicObject())
    {
        a.name = obj->getProperty("name").toString().toStdString();
        a.description = obj->getProperty("description").toString().toStdString();
        a.version = obj->getProperty("version").toString().toStdString();

        if (const auto* defs = obj->getProperty("definitions").getArray())
            for (const auto& item : *defs)
                a.definitions.push_back(SubsystemDefinition::fromJson(item));

        if (const auto* insts = obj->getProperty("instances").getArray())
            for (const auto& item : *insts)
                a.instances.push_back(SubsystemInstance::fromJson(item));

        if (const auto* conns = obj->getProperty("connections").getArray())
            for (const auto& item : *conns)
                a.connections.push_back(SignalConnection::fromJson(item));

        if (const auto* ports = obj->getProperty("systemPorts").getArray())
            for (const auto& item : *ports)
                a.systemPorts.push_back(SubsystemPort::fromJson(item));

        if (obj->hasProperty("mechanicalScene"))
            a.mechanicalScene = mechanical_scene::MechanicalScene::fromJson(obj->getProperty("mechanicalScene"));
    }
    return a;
}

bool SystemAssembly::saveToFile(const juce::File& file, juce::String& error) const
{
    const auto jsonStr = juce::JSON::toString(toJson(), false);
    if (!file.replaceWithText(jsonStr))
    {
        error = "Failed to write assembly file: " + file.getFullPathName();
        return false;
    }
    return true;
}

bool SystemAssembly::loadFromFile(const juce::File& file, SystemAssembly& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Assembly file not found: " + file.getFullPathName();
        return false;
    }

    const auto jsonStr = file.loadFileAsString();
    const auto parsed = juce::JSON::parse(jsonStr);
    if (!parsed.isObject())
    {
        error = "Invalid JSON in assembly file: " + file.getFullPathName();
        return false;
    }

    out = fromJson(parsed);
    const auto base = file.getParentDirectory();
    for (auto& def : out.definitions)
        if (!def.referenceFile.empty() && !juce::File::isAbsolutePath(juce::String(def.referenceFile)))
            def.referenceFile = base.getChildFile(juce::String(def.referenceFile)).getFullPathName().toStdString();
    return true;
}

// -----------------------------------------------------------------------------
// Coordinated Sequencer Programmable Device
// -----------------------------------------------------------------------------

class SequencerDevice final : public circuit_sim::ProgrammableDevice
{
public:
    explicit SequencerDevice(double motorBDelaySec)
        : motorBDelaySec_(motorBDelaySec)
    {
    }

    void reset() override
    {
        state = State::Off;
        trialState = state;
        startTime = 0.0;
        trialStartTime = 0.0;
    }

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        // Pins:
        // 0: Master Power (>2V = ON)
        // 1: Master Start (>2V = pressed)
        // 2: Master Stop (>2V = pressed)
        // 3: Master E-Stop (>2V = E-STOP ACTIVE)
        // 4: Master Reset (>2V = pressed)
        // 5: Start A out
        // 6: Start B out (delayed)
        // 7: Stop All out
        // 8: E-Stop All out
        // 9: System Ready lamp
        // 10: System Fault lamp

        const bool pwr = (v[0] > 2.0);
        const bool start = (v[1] > 2.0);
        const bool stop = (v[2] > 2.0);
        const bool estop = (v[3] > 2.0);
        const bool reset = (v[4] > 2.0);

        trialState = state;
        trialStartTime = startTime;

        if (!pwr)
        {
            trialState = State::Off;
        }
        else if (estop)
        {
            trialState = State::EStop;
        }
        else
        {
            switch (state)
            {
                case State::Off:
                    trialState = start ? State::Running : State::Ready;
                    if (start)
                        trialStartTime = t;
                    break;

                case State::Ready:
                case State::Stopped:
                    if (start)
                    {
                        trialState = State::Running;
                        trialStartTime = t;
                    }
                    break;

                case State::Running:
                    if (stop)
                        trialState = State::Stopped;
                    break;

                case State::EStop:
                case State::Fault:
                    if (reset && !estop)
                        trialState = State::Ready;
                    break;
            }
        }

        // Generate outputs
        double startAV = 0.0;
        double startBV = 0.0;
        double stopAllV = 0.0;
        double estopAllV = 0.0;

        if (trialState == State::Running)
        {
            startAV = 5.0; // Motor A starts immediately
            if ((t - trialStartTime) >= motorBDelaySec_)
                startBV = 5.0; // Motor B starts after programmed delay
        }
        else if (trialState == State::Stopped)
        {
            stopAllV = 5.0;
        }
        else if (trialState == State::EStop)
        {
            estopAllV = 5.0;
        }

        const double readyV = (trialState == State::Ready || trialState == State::Running) ? 5.0 : 0.0;
        const double faultV = (trialState == State::EStop || trialState == State::Fault) ? 5.0 : 0.0;

        i = {
            -v[0] / 1e6, -v[1] / 1e6, -v[2] / 1e6, -v[3] / 1e6, -v[4] / 1e6,
            (startAV - v[5]) / 100.0,
            (startBV - v[6]) / 100.0,
            (stopAllV - v[7]) / 100.0,
            (estopAllV - v[8]) / 100.0,
            (readyV - v[9]) / 100.0,
            (faultV - v[10]) / 100.0
        };

        return true;
    }

    void accept(double) override
    {
        state = trialState;
        startTime = trialStartTime;
    }

    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        out.resize(sizeof(State) + sizeof(double));
        std::uint8_t* p = out.data();
        std::memcpy(p, &state, sizeof(State)); p += sizeof(State);
        std::memcpy(p, &startTime, sizeof(double));
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() < sizeof(State) + sizeof(double)) return false;
        const std::uint8_t* p = in.data();
        std::memcpy(&state, p, sizeof(State)); p += sizeof(State);
        std::memcpy(&startTime, p, sizeof(double));
        trialState = state;
        trialStartTime = startTime;
        return true;
    }

private:
    enum class State { Off, Ready, Running, Stopped, EStop, Fault };
    double motorBDelaySec_ = 0.15;
    State state = State::Off;
    State trialState = State::Off;
    double startTime = 0.0;
    double trialStartTime = 0.0;
};

// -----------------------------------------------------------------------------
// Assembly Closed-Loop Controller Device
// -----------------------------------------------------------------------------

class ModularMotorControllerDevice final : public circuit_sim::ProgrammableDevice
{
public:
    ModularMotorControllerDevice(double kp, double ki, double kd, double vMax, double vMin)
        : kp_(kp), ki_(ki), kd_(kd), vMax_(vMax), vMin_(vMin)
    {
    }

    void reset() override
    {
        state = State::PowerOff;
        trialState = state;
        integral = 0.0;
        lastError = 0.0;
        lastT = 0.0;
        trialIntegral = 0.0;
        trialError = 0.0;
        hasLast = false;
    }

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        // Pins:
        // 0: pwr_in (>2V = ON)
        // 1: start_in (>2V = active)
        // 2: stop_in (>2V = active)
        // 3: estop_in (>2V = E-STOP ACTIVE)
        // 4: reset_in (>2V = reset)
        // 5: setpoint_in (10 mV per rad/s)
        // 6: feedback_in (10 mV per rad/s)
        // 7: drive_out
        // 8: run_lamp
        // 9: fault_lamp

        const bool pwr = (v[0] > 2.0);
        const bool start = (v[1] > 2.0);
        const bool stop = (v[2] > 2.0);
        const bool estop = (v[3] > 2.0);
        const bool reset = (v[4] > 2.0);

        const double wTarget = v[5] / 0.01;
        const double wActual = v[6] / 0.01;

        trialState = state;

        if (!pwr)
        {
            trialState = State::PowerOff;
        }
        else if (estop)
        {
            trialState = State::EStop;
        }
        else
        {
            switch (state)
            {
                case State::PowerOff:
                    trialState = State::Ready;
                    break;
                case State::Ready:
                case State::Stopped:
                    if (start)
                        trialState = State::Running;
                    break;
                case State::Running:
                    if (stop)
                        trialState = State::Stopped;
                    break;
                case State::EStop:
                case State::Faulted:
                    if (reset && !estop)
                        trialState = State::Ready;
                    break;
            }
        }

        double driveV = 0.0;
        const double dt = (hasLast && t > lastT) ? (t - lastT) : 0.0;
        const double err = wTarget - wActual;

        if (trialState == State::Running)
        {
            const double pTerm = kp_ * err;
            const double dTerm = (hasLast && dt > 1e-9) ? kd_ * (err - lastError) / dt : 0.0;
            const double uUnclamped = pTerm + integral * ki_ + dTerm;

            const bool satHigh = (uUnclamped >= vMax_);
            const bool satLow = (uUnclamped <= vMin_);
            const bool canIntegrate = (!satHigh && !satLow) || (satHigh && err < 0.0) || (satLow && err > 0.0);

            trialIntegral = integral + (canIntegrate ? err * dt : 0.0);
            trialError = err;

            driveV = std::clamp(pTerm + trialIntegral * ki_ + dTerm, vMin_, vMax_);
        }
        else
        {
            trialIntegral = 0.0;
            trialError = 0.0;
            driveV = 0.0;
        }

        const double runLampV = (trialState == State::Running) ? 5.0 : 0.0;
        const double faultLampV = (trialState == State::EStop || trialState == State::Faulted) ? 5.0 : 0.0;

        i = {
            -v[0] / 1e6, -v[1] / 1e6, -v[2] / 1e6, -v[3] / 1e6, -v[4] / 1e6,
            -v[5] / 1e6, -v[6] / 1e6,
            (driveV - v[7]) / 0.05,
            (runLampV - v[8]) / 100.0,
            (faultLampV - v[9]) / 100.0
        };

        return true;
    }

    void accept(double t) override
    {
        state = trialState;
        integral = trialIntegral;
        lastError = trialError;
        lastT = t;
        hasLast = true;
    }

    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        out.resize(sizeof(State) + sizeof(double) * 3 + sizeof(bool));
        std::uint8_t* p = out.data();
        std::memcpy(p, &state, sizeof(State)); p += sizeof(State);
        std::memcpy(p, &integral, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &lastError, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &lastT, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &hasLast, sizeof(bool));
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() < sizeof(State) + sizeof(double) * 3 + sizeof(bool)) return false;
        const std::uint8_t* p = in.data();
        std::memcpy(&state, p, sizeof(State)); p += sizeof(State);
        std::memcpy(&integral, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&lastError, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&lastT, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&hasLast, p, sizeof(bool));
        trialState = state;
        trialIntegral = integral;
        trialError = lastError;
        return true;
    }

private:
    enum class State { PowerOff, Ready, Running, Stopped, EStop, Faulted };
    double kp_ = 0.08, ki_ = 2.0, kd_ = 0.0002, vMax_ = 18.0, vMin_ = 0.0;
    State state = State::PowerOff;
    State trialState = State::PowerOff;
    double integral = 0.0;
    double lastError = 0.0;
    double lastT = 0.0;
    double trialIntegral = 0.0;
    double trialError = 0.0;
    bool hasLast = false;
};

// -----------------------------------------------------------------------------
// Assembly Compiler Implementation
// -----------------------------------------------------------------------------

namespace
{
std::string signalPath(const std::string& instance, const std::string& port)
{
    return (instance == "system" || instance.empty()) ? ("system." + port) : (instance + "." + port);
}

std::pair<std::string, std::string> splitSignalPath(const std::string& path)
{
    const auto dot = path.find('.');
    if (dot == std::string::npos)
        return { "system", path };
    return { path.substr(0, dot), path.substr(dot + 1) };
}

std::string safeElementSuffix(const std::string& path)
{
    juce::String s(path);
    return s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")
            .replaceCharacter('.', '_')
            .replaceCharacter('-', '_')
            .toStdString();
}

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

bool loadProgrammableDefinition(const SubsystemDefinition& def,
                                const juce::File& projectFolder,
                                frust_component::Definition& out,
                                juce::File& definitionFile,
                                juce::String& error)
{
    if (!def.referenceFile.empty())
    {
        const auto ref = resolveReferenceFile(def.referenceFile, projectFolder);
        if (ref.existsAsFile())
        {
            if (!frust_component::load(ref, out, error))
                return false;
            definitionFile = ref;
            return true;
        }

        if (!def.componentData.isObject())
        {
            error = "Programmable component definition file not found: " + ref.getFullPathName();
            return false;
        }
    }

    if (!def.componentData.isObject())
    {
        error = "Programmable component '" + juce::String(def.id) + "' has no component definition data or valid reference file.";
        return false;
    }

    if (!frust_component::fromVar(def.componentData, out, error))
    {
        error = "Programmable component '" + juce::String(def.id) + "' definition is invalid: " + error;
        return false;
    }

    definitionFile = resolveReferenceFile(def.referenceFile, projectFolder);
    return true;
}
}

CompiledAssemblyCircuit AssemblyCompiler::compile(const SystemAssembly& assembly, const juce::File& projectFolder)
{
    CompiledAssemblyCircuit res;
    circuit_sim::Circuit& c = res.circuit;

    // Node allocator map: "instance.port" -> circuit_sim::Node
    std::map<std::string, circuit_sim::Node> portNodes;

    // Resolve nets using Disjoint Set Union (DSU) so multi-drop connections
    // (e.g. sequencer outputs fanning out to multiple controllers) correctly merge
    std::map<std::string, std::string> parentNet;
    auto findRoot = [&](auto& self, const std::string& x) -> std::string {
        auto it = parentNet.find(x);
        if (it == parentNet.end() || it->second == x)
            return parentNet[x] = x;
        return parentNet[x] = self(self, it->second);
    };
    auto uniteNets = [&](const std::string& a, const std::string& b) {
        std::string ra = findRoot(findRoot, a);
        std::string rb = findRoot(findRoot, b);
        if (ra != rb)
            parentNet[ra] = rb;
    };

    for (const auto& conn : assembly.connections)
    {
        uniteNets(signalPath(conn.fromInstance, conn.fromPort), signalPath(conn.toInstance, conn.toPort));
    }

    std::map<std::string, circuit_sim::Node> netToNode;
    auto getNodeForPort = [&](const std::string& instanceId, const std::string& portName) -> circuit_sim::Node {
        const std::string fullPath = instanceId + "." + portName;
        auto it = portNodes.find(fullPath);
        if (it != portNodes.end())
            return it->second;

        const std::string root = findRoot(findRoot, fullPath);
        auto existingNetNode = netToNode.find(root);
        if (existingNetNode != netToNode.end())
        {
            portNodes[fullPath] = existingNetNode->second;
            return existingNetNode->second;
        }

        const auto node = c.addNode();
        netToNode[root] = node;
        portNodes[fullPath] = node;
        return node;
    };

    auto bindExistingNode = [&](const std::string& path, circuit_sim::Node node) {
        const auto root = findRoot(findRoot, path);
        portNodes[path] = node;
        netToNode[root] = node;
    };

    // Instantiate Master System-Level Inputs
    circuit_sim::Waveform dc0; dc0.offset = 0.0;

    const auto pwrNode = c.addNode();
    const auto startNode = c.addNode();
    const auto stopNode = c.addNode();
    const auto estopNode = c.addNode();
    const auto resetNode = c.addNode();

    const int pwrElem = c.addVoltageSource("V_SYS_PWR", pwrNode, 0, dc0);
    const int startElem = c.addVoltageSource("V_SYS_START", startNode, 0, dc0);
    const int stopElem = c.addVoltageSource("V_SYS_STOP", stopNode, 0, dc0);
    const int estopElem = c.addVoltageSource("V_SYS_ESTOP", estopNode, 0, dc0);
    const int resetElem = c.addVoltageSource("V_SYS_RESET", resetNode, 0, dc0);

    res.inputElements["power_switch"] = pwrElem;
    res.inputElements["system.power"] = pwrElem;
    res.inputElements["start_button"] = startElem;
    res.inputElements["system.start"] = startElem;
    res.inputElements["stop_button"] = stopElem;
    res.inputElements["system.stop"] = stopElem;
    res.inputElements["estop_button"] = estopElem;
    res.inputElements["system.estop"] = estopElem;
    res.inputElements["reset_button"] = resetElem;
    res.inputElements["system.reset"] = resetElem;

    res.signalNodes["system.power"] = pwrNode;
    res.signalNodes["system.start"] = startNode;
    res.signalNodes["system.stop"] = stopNode;
    res.signalNodes["system.estop"] = estopNode;
    res.signalNodes["system.reset"] = resetNode;

    bindExistingNode("system.power", pwrNode);
    bindExistingNode("system.start", startNode);
    bindExistingNode("system.stop", stopNode);
    bindExistingNode("system.estop", estopNode);
    bindExistingNode("system.reset", resetNode);

    auto addGenericVoltageInput = [&](const std::string& path, double defaultValue) {
        if (res.inputElements.find(path) != res.inputElements.end())
            return;
        const auto [instance, port] = splitSignalPath(path);
        const auto node = getNodeForPort(instance, port);
        circuit_sim::Waveform dc;
        dc.offset = defaultValue;
        const int elem = c.addVoltageSource("V_IN_" + safeElementSuffix(path), node, 0, dc);
        res.inputElements[path] = elem;
        res.signalNodes[path] = node;
        if (instance == "system")
            res.inputElements[port] = elem;
    };

    for (const auto& port : assembly.systemPorts)
    {
        if (port.direction == PortDirection::Output)
            continue;

        const auto path = port.name.find('.') == std::string::npos ? ("system." + port.name) : port.name;
        if (res.inputElements.find(path) != res.inputElements.end())
            continue;

        if (port.domain == PortDomain::SignalControl || port.domain == PortDomain::Electrical)
            addGenericVoltageInput(path, 0.0);
    }

    // Instantiate Subsystem Instances
    for (const auto& inst : assembly.instances)
    {
        const auto* def = assembly.findDefinition(inst.definitionId);
        if (!def)
        {
            res.error = "Unknown subsystem definition: " + inst.definitionId;
            return res;
        }

        if (def->kind == SubsystemKind::CoordinatedSequencer)
        {
            const double delaySec = inst.getParam("motor_b_delay_sec", 0.15);
            auto seqDev = std::make_shared<SequencerDevice>(delaySec);

            // Connect inputs to system master nodes
            const auto nodeStartA = getNodeForPort(inst.instanceId, "start_a_out");
            const auto nodeStartB = getNodeForPort(inst.instanceId, "start_b_out");
            const auto nodeStopAll = getNodeForPort(inst.instanceId, "stop_all_out");
            const auto nodeEstopAll = getNodeForPort(inst.instanceId, "estop_all_out");
            const auto nodeReady = getNodeForPort(inst.instanceId, "system_ready");
            const auto nodeFault = getNodeForPort(inst.instanceId, "system_fault");

            c.addProgrammable("U_" + inst.instanceId,
                             { pwrNode, startNode, stopNode, estopNode, resetNode,
                               nodeStartA, nodeStartB, nodeStopAll, nodeEstopAll, nodeReady, nodeFault },
                             seqDev);

            res.signalNodes[inst.instanceId + ".start_a"] = nodeStartA;
            res.signalNodes[inst.instanceId + ".start_b"] = nodeStartB;
            res.signalNodes[inst.instanceId + ".ready"] = nodeReady;
            res.signalNodes[inst.instanceId + ".fault"] = nodeFault;
            res.signalNodes["system.ready"] = nodeReady;
            res.signalNodes["system.fault"] = nodeFault;
        }
        else if (def->kind == SubsystemKind::MotorSpeedController)
        {
            const double kp = inst.getParam("kp", 0.08);
            const double ki = inst.getParam("ki", 2.0);
            const double kd = inst.getParam("kd", 0.0002);
            const double vMax = inst.getParam("v_max", 18.0);
            const double vMin = inst.getParam("v_min", 0.0);

            auto ctlDev = std::make_shared<ModularMotorControllerDevice>(kp, ki, kd, vMax, vMin);

            // Port nodes
            const auto nStart = getNodeForPort(inst.instanceId, "start_in");
            const auto nStop = getNodeForPort(inst.instanceId, "stop_in");
            const auto nEstop = getNodeForPort(inst.instanceId, "estop_in");
            const auto nReset = getNodeForPort(inst.instanceId, "reset_in");
            const auto nSetpoint = getNodeForPort(inst.instanceId, "setpoint_in");
            const auto nFeedback = getNodeForPort(inst.instanceId, "feedback_in");
            const auto nDrive = getNodeForPort(inst.instanceId, "drive_out");
            const auto nRunLamp = getNodeForPort(inst.instanceId, "run_lamp");
            const auto nFaultLamp = getNodeForPort(inst.instanceId, "fault_lamp");

            // Add setpoint voltage source
            circuit_sim::Waveform dcSet;
            const double initSetpoint = (inst.instanceId == "controllerB" || inst.instanceId.find("B") != std::string::npos) ? 2.0 : 3.0; // 200 or 300 rad/s
            dcSet.offset = initSetpoint;
            const int setpointElem = c.addVoltageSource("V_SET_" + inst.instanceId, nSetpoint, 0, dcSet);

            res.inputElements[inst.instanceId + ".speed_setpoint"] = setpointElem;
            if (inst.instanceId == "controllerA")
            {
                res.inputElements["motorA.speed_setpoint"] = setpointElem;
                res.inputElements["speed_setpoint"] = setpointElem;
            }
            else if (inst.instanceId == "controllerB")
            {
                res.inputElements["motorB.speed_setpoint"] = setpointElem;
            }

            c.addProgrammable("U_" + inst.instanceId,
                             { pwrNode, nStart, nStop, nEstop, nReset,
                               nSetpoint, nFeedback, nDrive, nRunLamp, nFaultLamp },
                             ctlDev);

            // Terminate lamps
            c.addResistor("R_RUN_" + inst.instanceId, nRunLamp, 0, 1000.0);
            c.addResistor("R_FLT_" + inst.instanceId, nFaultLamp, 0, 1000.0);

            res.signalNodes[inst.instanceId + ".setpoint"] = nSetpoint;
            res.signalNodes[inst.instanceId + ".drive"] = nDrive;
            res.signalNodes[inst.instanceId + ".running"] = nRunLamp;
            res.signalNodes[inst.instanceId + ".fault"] = nFaultLamp;
        }
        else if (def->kind == SubsystemKind::DCMotor)
        {
            const auto nVin = getNodeForPort(inst.instanceId, "v_in");
            const auto nTach = getNodeForPort(inst.instanceId, "tach");
            const auto nAngle = getNodeForPort(inst.instanceId, "angle");

            mech::DcMotorModel m;
            m.ra = inst.getParam("ra", 2.0);
            m.la = inst.getParam("la", 0.001);
            m.kt = inst.getParam("kt", 0.02);
            m.ke = inst.getParam("ke", 0.02);
            m.j = inst.getParam("j", 0.00001);
            m.b = inst.getParam("b", 0.000001);

            mech::MotorLoad load;
            mech::ShaftModel shaft;

            const auto motor = mech::addDcMotor(c, inst.instanceId, nVin, 0, m, load, shaft);
            mech::addTachometer(c, "TG_" + inst.instanceId, motor.shaft, nTach, 0, 0.01, 100.0);

            // Add mechanical load current source
            circuit_sim::Waveform dcLoad; dcLoad.offset = 0.0;
            const int loadElem = c.addCurrentSource("TL_" + inst.instanceId, motor.shaft, 0, dcLoad);

            res.inputElements[inst.instanceId + ".load_torque"] = loadElem;
            if (inst.instanceId == "motorA")
                res.inputElements["load_torque"] = loadElem;

            res.signalNodes[inst.instanceId + ".speed"] = motor.shaft;
            res.signalNodes[inst.instanceId + ".angle"] = motor.angle;
            res.signalNodes[inst.instanceId + ".voltage"] = nVin;
            res.signalBranches[inst.instanceId + ".current"] = motor.armature;
        }
        else if (def->kind == SubsystemKind::SchematicReference)
        {
            // User-defined schematic diagram subsystem
            juce::var circuitData = def->schematicData;
            if (!circuitData.isObject() && !def->referenceFile.empty())
            {
                juce::File refFile(juce::String(def->referenceFile));
                if (refFile.existsAsFile())
                {
                    circuitData = juce::JSON::parse(refFile);
                    if (const auto* obj = circuitData.getDynamicObject(); obj && obj->hasProperty("circuit"))
                        circuitData = obj->getProperty("circuit");
                }
            }

            const auto* root = circuitData.getDynamicObject();
            const auto* comps = root != nullptr ? root->getProperty("components").getArray() : nullptr;

            if (comps == nullptr)
            {
                res.error = "Schematic subsystem '" + inst.instanceId + "' has no valid components.";
                return res;
            }

            // Map internal nets of this schematic to circuit nodes
            // First, find port-to-net mappings from block_port / power_port symbols
            std::map<std::string, std::string> portToInternalNet;
            for (const auto& entry : *comps)
            {
                if (const auto* co = entry.getDynamicObject())
                {
                    const auto sym = co->getProperty("symbol").toString();
                    if (sym == "block_port" || sym == "power_port" || sym == "net_label")
                    {
                        const auto pName = co->getProperty("busName").toString().trim();
                        const auto name = pName.isNotEmpty() ? pName : co->getProperty("id").toString();
                        if (const auto* pins = co->getProperty("pins").getDynamicObject())
                        {
                            for (const auto& p : pins->getProperties())
                            {
                                portToInternalNet[name.toStdString()] = p.value.toString().toStdString();
                            }
                        }
                    }
                }
            }

            // Internal net resolver for this instance
            std::map<std::string, circuit_sim::Node> internalNetNodes;
            auto getNetNode = [&](const std::string& netName) -> circuit_sim::Node {
                if (netName == "0" || netName == "gnd" || netName.empty())
                    return 0;

                // Check if this net matches an external port of the instance
                for (const auto& [pName, mappedNet] : portToInternalNet)
                {
                    if (mappedNet == netName)
                        return getNodeForPort(inst.instanceId, pName);
                }

                auto it = internalNetNodes.find(netName);
                if (it != internalNetNodes.end())
                    return it->second;

                const auto node = c.addNode();
                internalNetNodes[netName] = node;
                return node;
            };

            // Instantiate components from the schematic
            for (const auto& entry : *comps)
            {
                const auto* co = entry.getDynamicObject();
                if (!co) continue;

                const auto sym = co->getProperty("symbol").toString();
                const auto refdes = co->getProperty("id").toString().toStdString();
                const auto* pinsObj = co->getProperty("pins").getDynamicObject();

                auto getPinNode = [&](const std::string& pinName, const std::string& fallbackNet = "") -> circuit_sim::Node {
                    if (pinsObj && pinsObj->hasProperty(juce::String(pinName)))
                        return getNetNode(pinsObj->getProperty(juce::String(pinName)).toString().toStdString());
                    if (!fallbackNet.empty())
                        return getNetNode(fallbackNet);
                    return c.addNode();
                };

                if (sym == "block_port" || sym == "power_port" || sym == "net_label" || sym == "annotation_text")
                {
                    continue;
                }
                else if (sym == "dc_motor")
                {
                    mech::DcMotorModel m;
                    m.ra = inst.getParam("ra", 2.0);
                    m.la = inst.getParam("la", 0.001);
                    m.kt = inst.getParam("kt", 0.02);
                    m.ke = inst.getParam("ke", 0.02);
                    m.j  = inst.getParam("j", 0.00001);
                    m.b  = inst.getParam("b", 0.000001);

                    mech::MotorLoad load;
                    mech::ShaftModel shaft;

                    const auto nPos = getPinNode("pos", "1");
                    const auto nNeg = getPinNode("neg", "2");

                    const auto motor = mech::addDcMotor(c, inst.instanceId + "_" + refdes, nPos, nNeg, m, load, shaft);

                    // If schematic has load_torque or speed ports, tie their nodes to motor.shaft
                    for (const auto& [pName, mappedNet] : portToInternalNet)
                    {
                        if (pName == "speed" || pName == "shaft" || pName == "load_torque")
                        {
                            const auto pNode = getNodeForPort(inst.instanceId, pName);
                            c.addResistor("R_CPL_" + inst.instanceId + "_" + pName, motor.shaft, pNode, 1e-4);
                        }
                    }

                    // Load torque current source
                    circuit_sim::Waveform dcLoad; dcLoad.offset = 0.0;
                    const int loadElem = c.addCurrentSource("TL_" + inst.instanceId + "_" + refdes, motor.shaft, 0, dcLoad);

                    res.inputElements[inst.instanceId + ".load_torque"] = loadElem;
                    if (inst.instanceId == "motorDriveA" || inst.instanceId == "motorA")
                    {
                        res.inputElements["motorA.load_torque"] = loadElem;
                        res.inputElements["load_torque"] = loadElem;
                    }
                    else if (inst.instanceId == "motorDriveB" || inst.instanceId == "motorB")
                    {
                        res.inputElements["motorB.load_torque"] = loadElem;
                    }

                    res.signalNodes[inst.instanceId + ".speed"] = motor.shaft;
                    res.signalNodes[inst.instanceId + "." + refdes + ".speed"] = motor.shaft;
                    res.signalNodes[inst.instanceId + ".angle"] = motor.angle;
                    res.signalNodes[inst.instanceId + ".voltage"] = nPos;
                    res.signalBranches[inst.instanceId + ".current"] = motor.armature;
                    res.signalBranches[inst.instanceId + "." + refdes + ".current"] = motor.armature;

                    // Aliases for compatibility with dual motor panel
                    if (inst.instanceId == "motorDriveA")
                    {
                        res.signalNodes["motorA.speed"] = motor.shaft;
                        res.signalBranches["motorA.current"] = motor.armature;
                    }
                    else if (inst.instanceId == "motorDriveB")
                    {
                        res.signalNodes["motorB.speed"] = motor.shaft;
                        res.signalBranches["motorB.current"] = motor.armature;
                    }
                }
                else if (sym == "tachometer")
                {
                    auto itSpeed = res.signalNodes.find(inst.instanceId + ".speed");
                    const circuit_sim::Node shaftNode = (itSpeed != res.signalNodes.end()) ? itSpeed->second : c.addNode();
                    const auto nOut = getPinNode("out", "1");
                    mech::addTachometer(c, "TG_" + inst.instanceId + "_" + refdes, shaftNode, nOut, 0, 0.01, 100.0);
                    res.signalNodes[inst.instanceId + ".tach"] = nOut;
                }
                else if (sym == "current_source")
                {
                    circuit_sim::Waveform wave;
                    const auto n1 = getPinNode("1");
                    const auto n2 = getPinNode("2");
                    const int elem = c.addCurrentSource(inst.instanceId + "_" + refdes, n1, n2, wave);
                    if (refdes.find("TL") != std::string::npos || refdes.find("load") != std::string::npos)
                    {
                        res.inputElements[inst.instanceId + ".load_torque"] = elem;
                    }
                }
                else if (sym == "resistor")
                {
                    double ohms = 1000.0;
                    const auto valStr = co->getProperty("value").toString();
                    circuit_sim::parseValue(valStr.toStdString(), ohms);
                    ohms = inst.getParam(refdes, ohms);
                    c.addResistor(inst.instanceId + "_" + refdes, getPinNode("1"), getPinNode("2"), ohms);
                }
                else if (sym == "capacitor")
                {
                    double farads = 1e-6;
                    circuit_sim::parseValue(co->getProperty("value").toString().toStdString(), farads);
                    c.addCapacitor(inst.instanceId + "_" + refdes, getPinNode("1"), getPinNode("2"), farads);
                }
                else if (sym == "inductor")
                {
                    double henries = 1e-3;
                    circuit_sim::parseValue(co->getProperty("value").toString().toStdString(), henries);
                    c.addInductor(inst.instanceId + "_" + refdes, getPinNode("1"), getPinNode("2"), henries);
                }
            }

            for (const auto& p : def->ports)
            {
                res.signalNodes[inst.instanceId + "." + p.name] = getNodeForPort(inst.instanceId, p.name);
            }
        }
        else if (def->kind == SubsystemKind::ProgrammableComponent)
        {
            frust_component::Definition componentDefinition;
            juce::File definitionFile;
            juce::String componentError;
            if (!loadProgrammableDefinition(*def, projectFolder, componentDefinition, definitionFile, componentError))
            {
                res.error = componentError.toStdString();
                return res;
            }

            const auto programFile = frust_component::programFileFor(componentDefinition, definitionFile, projectFolder);
            if (!frust_component::compileProgramFile(componentDefinition, programFile, componentError))
            {
                res.error = componentError.toStdString();
                return res;
            }

            std::map<juce::String, double> parameterValues;
            for (const auto& p : componentDefinition.parameters)
            {
                auto it = inst.numericParams.find(p.name.toStdString());
                if (it != inst.numericParams.end())
                    parameterValues[p.name] = it->second;
            }

            auto device = frust_component::Library::instance().makeDevice(componentDefinition, parameterValues,
                                                                          juce::String(inst.instanceId), componentError);
            if (device == nullptr)
            {
                res.error = componentError.toStdString();
                return res;
            }

            std::vector<circuit_sim::Node> pins;
            for (const auto& pin : componentDefinition.pins)
            {
                const auto node = getNodeForPort(inst.instanceId, pin.id.toStdString());
                pins.push_back(node);
                res.signalNodes[inst.instanceId + "." + pin.id.toStdString()] = node;
            }

            if (pins.empty())
            {
                res.error = "Programmable component '" + inst.instanceId + "' exposes no pins.";
                return res;
            }

            c.addProgrammable("U_" + inst.instanceId, pins, std::move(device));
        }
    }

    res.success = true;
    return res;
}

} // namespace sim_panel

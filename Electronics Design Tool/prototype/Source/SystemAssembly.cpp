#include "SystemAssembly.h"

#include <algorithm>
#include <cmath>
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

// -----------------------------------------------------------------------------
// SubsystemInstance JSON
// -----------------------------------------------------------------------------

juce::var SubsystemInstance::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("instanceId", juce::String(instanceId));
    obj->setProperty("definitionId", juce::String(definitionId));
    obj->setProperty("title", juce::String(title));

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
    obj->setProperty("fromInstance", juce::String(fromInstance));
    obj->setProperty("fromPort", juce::String(fromPort));
    obj->setProperty("toInstance", juce::String(toInstance));
    obj->setProperty("toPort", juce::String(toPort));
    obj->setProperty("netName", juce::String(netName));
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

    return a;
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
                    trialState = State::Running;
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

CompiledAssemblyCircuit AssemblyCompiler::compile(const SystemAssembly& assembly)
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
        uniteNets(conn.sourcePath(), conn.destPath());
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
    }

    res.success = true;
    return res;
}

} // namespace sim_panel

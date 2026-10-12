#include <JuceHeader.h>

#include "../../Source/FrustComponent.h"
#include "../../Source/FrustComponentCompiler.h"
#include "../../Source/MachineModel.h"
#include "../../Source/NodeDesignerPanel.h"
#include "../../Source/SystemAssembly.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <tuple>

namespace
{
int checks = 0;
int failures = 0;

void check(bool ok, const juce::String& name, const juce::String& detail = {})
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(),
                ok || detail.isEmpty() ? "" : "  -- ",
                ok || detail.isEmpty() ? "" : detail.toRawUTF8());
    std::fflush(stdout);
}

void add(NodeDesignerPanel& p, const juce::String& type, const juce::String& id,
         juce::String& error, const juce::String& text = {})
{
    juce::String e;
    if (p.addNodeOfType(type, 0.0f, 0.0f, id, e).isEmpty())
    {
        error << "add " << id << ": " << e << "\n";
        return;
    }
    if (text.isNotEmpty() && !p.setNodeParameter(id, "text", text, e))
        error << "text " << id << ": " << e << "\n";
}

void wire(NodeDesignerPanel& p, const juce::String& from, const juce::String& fromPin,
          const juce::String& to, const juce::String& toPin, juce::String& error)
{
    juce::String e;
    if (p.connect(from, fromPin, to, toPin, e).isEmpty())
        error << "wire " << from << "." << fromPin << " -> " << to << "." << toPin << ": " << e << "\n";
}

bool buildControllerProgram(const frust_component::Definition& definition,
                            const juce::File& programFile,
                            juce::String& error)
{
    NodeDesignerPanel p;
    p.newGraph("node_graph", false);
    p.setComponentContext(definition.compilerContext());

    for (const auto& item : {
            std::tuple<const char*, const char*, const char*>{ "literal_f64", "zero", "0.0" },
            { "literal_f64", "one", "1.0" },
            { "literal_f64", "half", "0.5" },
            { "literal_f64", "threshold", "2.0" },
            { "literal_f64", "lamp_hi", "5.0" } })
        add(p, std::get<0>(item), std::get<1>(item), error, std::get<2>(item));

    for (const auto& pin : { "pwr_in", "start_in", "stop_in", "estop_in", "setpoint_in", "feedback_in" })
        add(p, "pc_pin_voltage", pin, error, pin);
    for (const auto& param : { "kp", "v_max", "v_min" })
        add(p, "pc_parameter", param, error, param);

    add(p, "pc_state_get", "prev_run", error, "run");
    add(p, "gt", "pwr_high", error);
    add(p, "gt", "start_high", error);
    add(p, "gt", "stop_high", error);
    add(p, "gt", "estop_high", error);
    add(p, "gt", "prev_run_high", error);
    add(p, "if", "pwr_num", error);
    add(p, "if", "start_num", error);
    add(p, "if", "stop_num", error);
    add(p, "if", "estop_num", error);
    add(p, "if", "prev_run_num", error);
    add(p, "add", "run_request_sum", error);
    add(p, "gt", "run_request", error);
    add(p, "if", "run_request_num", error);
    add(p, "add", "inhibit_sum", error);
    add(p, "lt", "not_inhibited", error);
    add(p, "if", "not_inhibited_num", error);
    add(p, "mul", "pwr_and_request", error);
    add(p, "mul", "run_num", error);
    add(p, "pc_state_set", "save_run", error, "run");

    add(p, "sub", "speed_error", error);
    add(p, "mul", "raw_drive", error);
    add(p, "gt", "above_max", error);
    add(p, "lt", "below_min", error);
    add(p, "if", "clamp_high", error);
    add(p, "if", "clamped_drive", error);
    add(p, "mul", "enabled_drive", error);
    add(p, "pc_drive", "drive_motor", error, "drive_out");

    add(p, "mul", "run_lamp_v", error);
    add(p, "pc_drive", "drive_run_lamp", error, "run_lamp");
    add(p, "if", "fault_lamp_v", error);
    add(p, "pc_drive", "drive_fault_lamp", error, "fault_lamp");

    auto gtThreshold = [&](const char* value, const char* gtNode) {
        wire(p, value, "volts", gtNode, "a", error);
        wire(p, "threshold", "value", gtNode, "b", error);
    };
    gtThreshold("pwr_in", "pwr_high");
    gtThreshold("start_in", "start_high");
    gtThreshold("stop_in", "stop_high");
    gtThreshold("estop_in", "estop_high");
    wire(p, "prev_run", "value", "prev_run_high", "a", error);
    wire(p, "half", "value", "prev_run_high", "b", error);

    auto boolToNum = [&](const char* boolNode, const char* ifNode) {
        wire(p, boolNode, "is greater", ifNode, "cond", error);
        wire(p, "one", "value", ifNode, "then", error);
        wire(p, "zero", "value", ifNode, "else", error);
    };
    boolToNum("pwr_high", "pwr_num");
    boolToNum("start_high", "start_num");
    boolToNum("stop_high", "stop_num");
    boolToNum("estop_high", "estop_num");
    boolToNum("prev_run_high", "prev_run_num");

    wire(p, "start_num", "value", "run_request_sum", "a", error);
    wire(p, "prev_run_num", "value", "run_request_sum", "b", error);
    wire(p, "run_request_sum", "sum", "run_request", "a", error);
    wire(p, "half", "value", "run_request", "b", error);
    wire(p, "run_request", "is greater", "run_request_num", "cond", error);
    wire(p, "one", "value", "run_request_num", "then", error);
    wire(p, "zero", "value", "run_request_num", "else", error);

    wire(p, "stop_num", "value", "inhibit_sum", "a", error);
    wire(p, "estop_num", "value", "inhibit_sum", "b", error);
    wire(p, "inhibit_sum", "sum", "not_inhibited", "a", error);
    wire(p, "half", "value", "not_inhibited", "b", error);
    wire(p, "not_inhibited", "is less", "not_inhibited_num", "cond", error);
    wire(p, "one", "value", "not_inhibited_num", "then", error);
    wire(p, "zero", "value", "not_inhibited_num", "else", error);

    wire(p, "pwr_num", "value", "pwr_and_request", "a", error);
    wire(p, "run_request_num", "value", "pwr_and_request", "b", error);
    wire(p, "pwr_and_request", "product", "run_num", "a", error);
    wire(p, "not_inhibited_num", "value", "run_num", "b", error);
    wire(p, "run_num", "product", "save_run", "value", error);

    wire(p, "setpoint_in", "volts", "speed_error", "a", error);
    wire(p, "feedback_in", "volts", "speed_error", "b", error);
    wire(p, "speed_error", "difference", "raw_drive", "a", error);
    wire(p, "kp", "value", "raw_drive", "b", error);
    wire(p, "raw_drive", "product", "above_max", "a", error);
    wire(p, "v_max", "value", "above_max", "b", error);
    wire(p, "raw_drive", "product", "below_min", "a", error);
    wire(p, "v_min", "value", "below_min", "b", error);
    wire(p, "above_max", "is greater", "clamp_high", "cond", error);
    wire(p, "v_max", "value", "clamp_high", "then", error);
    wire(p, "raw_drive", "product", "clamp_high", "else", error);
    wire(p, "below_min", "is less", "clamped_drive", "cond", error);
    wire(p, "v_min", "value", "clamped_drive", "then", error);
    wire(p, "clamp_high", "value", "clamped_drive", "else", error);
    wire(p, "clamped_drive", "value", "enabled_drive", "a", error);
    wire(p, "run_num", "product", "enabled_drive", "b", error);
    wire(p, "enabled_drive", "product", "drive_motor", "value", error);

    wire(p, "run_num", "product", "run_lamp_v", "a", error);
    wire(p, "lamp_hi", "value", "run_lamp_v", "b", error);
    wire(p, "run_lamp_v", "product", "drive_run_lamp", "value", error);
    wire(p, "estop_high", "is greater", "fault_lamp_v", "cond", error);
    wire(p, "lamp_hi", "value", "fault_lamp_v", "then", error);
    wire(p, "zero", "value", "fault_lamp_v", "else", error);
    wire(p, "fault_lamp_v", "value", "drive_fault_lamp", "value", error);

    if (error.isNotEmpty())
        return false;

    if (!p.saveToFile(programFile, error))
        return false;

    juce::String compileError;
    if (!frust_component::compileProgramFile(definition, programFile, compileError))
    {
        error = compileError;
        return false;
    }

    return true;
}

juce::var motorDriveSchematic()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 2);
    root->setProperty("kind", "electronics_circuit");
    root->setProperty("name", "Saved Motor Drive Schematic");
    root->setProperty("description", "DC motor, tachometer, and exposed shaft/load ports.");

    juce::Array<juce::var> components;
    auto port = [&](const char* id, const char* name, const char* net) {
        auto* p = new juce::DynamicObject();
        p->setProperty("id", id);
        p->setProperty("symbol", "block_port");
        p->setProperty("busName", name);
        auto* pins = new juce::DynamicObject();
        pins->setProperty("1", net);
        p->setProperty("pins", juce::var(pins));
        components.add(juce::var(p));
    };
    port("PORT_VIN", "v_in", "NET_VIN");
    port("PORT_LOAD", "load_torque", "NET_SHAFT");
    port("PORT_SPEED", "speed", "NET_SHAFT");
    port("PORT_TACH", "tach", "NET_TACH");

    auto* motor = new juce::DynamicObject();
    motor->setProperty("id", "M1");
    motor->setProperty("symbol", "dc_motor");
    auto* mPins = new juce::DynamicObject();
    mPins->setProperty("pos", "NET_VIN");
    mPins->setProperty("neg", "0");
    mPins->setProperty("shaft", "NET_SHAFT");
    mPins->setProperty("angle", "NET_ANGLE");
    motor->setProperty("pins", juce::var(mPins));
    components.add(juce::var(motor));

    auto* tach = new juce::DynamicObject();
    tach->setProperty("id", "TG1");
    tach->setProperty("symbol", "tachometer");
    auto* tPins = new juce::DynamicObject();
    tPins->setProperty("shaft", "NET_SHAFT");
    tPins->setProperty("out", "NET_TACH");
    tach->setProperty("pins", juce::var(tPins));
    components.add(juce::var(tach));

    root->setProperty("components", components);
    return juce::var(root);
}

frust_component::Definition controllerDefinition()
{
    frust_component::Definition d;
    d.name = "SavedMotorController";
    d.description = "Saved FRust/node proportional motor controller with run latch.";
    d.outputResistance = 0.05;
    d.programFile = "SavedMotorController.frnode.json";
    d.pins = {
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
    d.parameters = {
        { "kp", 12.0 },
        { "v_max", 18.0 },
        { "v_min", 0.0 }
    };
    d.state = {
        { "run", 0.0 }
    };
    return d;
}

bool writeSavedModel(const juce::File& folder, double kp, double inertia,
                     juce::File& machineFile, juce::String& error)
{
    folder.createDirectory();

    const auto schematicFile = folder.getChildFile("motor_drive.diagram.json");
    if (!schematicFile.replaceWithText(juce::JSON::toString(motorDriveSchematic(), true)))
    {
        error = "Could not write " + schematicFile.getFullPathName();
        return false;
    }

    auto controller = controllerDefinition();
    const auto componentFile = folder.getChildFile("SavedMotorController.frcomp.json");
    if (!frust_component::save(componentFile, controller, error))
        return false;
    if (!buildControllerProgram(controller, componentFile.getSiblingFile(controller.programFile), error))
        return false;

    sim_panel::SubsystemDefinition driveDef;
    if (!sim_panel::SubsystemDefinition::loadSchematicDefinition(schematicFile, driveDef, error))
        return false;
    driveDef.id = "motor_drive";
    driveDef.referenceFile = schematicFile.getFileName().toStdString();

    sim_panel::SubsystemDefinition controllerDef;
    if (!sim_panel::SubsystemDefinition::loadFrustComponentDefinition(componentFile, controllerDef, error))
        return false;
    controllerDef.id = "motor_controller";
    controllerDef.referenceFile = componentFile.getFileName().toStdString();

    machine_builder::MachineDesign machine = machine_builder::MachineDesign::createBlankMachine("Saved Model Motor Controller");
    machine.id = "saved_model_motor_controller";
    machine.description = "Saved schematic plus saved FRust/node controller executed through AssemblyCompiler.";
    machine.assembly.name = "Saved Model Motor Controller Assembly";
    machine.assembly.definitions = { driveDef, controllerDef };

    sim_panel::SubsystemInstance drive;
    drive.instanceId = "drive";
    drive.definitionId = "motor_drive";
    drive.title = "Saved Schematic Motor Drive";
    drive.numericParams = {
        { "ra", 2.0 }, { "la", 0.001 }, { "kt", 0.02 },
        { "ke", 0.02 }, { "j", inertia }, { "b", 0.000001 }
    };
    machine.assembly.instances.push_back(drive);

    sim_panel::SubsystemInstance ctl;
    ctl.instanceId = "controller";
    ctl.definitionId = "motor_controller";
    ctl.title = "Saved FRust Controller";
    ctl.numericParams = { { "kp", kp }, { "v_max", 18.0 }, { "v_min", 0.0 } };
    machine.assembly.instances.push_back(ctl);

    machine.assembly.systemPorts = {
        { "power", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "V", "Master Power" },
        { "start", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "V", "Start" },
        { "stop", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "V", "Stop" },
        { "estop", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "V", "E-Stop" },
        { "reset", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "V", "Reset" },
        { "speed_setpoint", sim_panel::PortDirection::Input, sim_panel::PortDomain::SignalControl, "rad/s", "Commanded speed" }
    };

    machine.assembly.connections = {
        { "system", "power", "controller", "pwr_in", "NET_PWR" },
        { "system", "start", "controller", "start_in", "NET_START" },
        { "system", "stop", "controller", "stop_in", "NET_STOP" },
        { "system", "estop", "controller", "estop_in", "NET_ESTOP" },
        { "system", "reset", "controller", "reset_in", "NET_RESET" },
        { "system", "speed_setpoint", "controller", "setpoint_in", "NET_SETPOINT" },
        { "controller", "drive_out", "drive", "v_in", "NET_DRIVE" },
        { "drive", "tach", "controller", "feedback_in", "NET_TACH" }
    };

    machine.controlPanel.widgets = {
        { "pwr", sim_panel::WidgetType::ToggleSwitch, "Master Power", "system.power", "", 0.0, 1.0, 0.0, 1.0, 0, 0, 50, {} },
        { "start", sim_panel::WidgetType::PushButton, "Start", "system.start", "", 0.0, 1.0, 0.0, 1.0, 0, 1, 45, {} },
        { "stop", sim_panel::WidgetType::PushButton, "Stop", "system.stop", "", 0.0, 1.0, 0.0, 1.0, 0, 2, 45, {} },
        { "setpoint", sim_panel::WidgetType::Slider, "Speed Command", "system.speed_setpoint", "rad/s", 0.0, 600.0, 300.0, 5.0, 0, 3, 80, {} },
        { "load", sim_panel::WidgetType::Slider, "Load Torque", "drive.load_torque", "N*m", 0.0, 0.05, 0.0, 0.001, 0, 4, 80, {} },
        { "speed", sim_panel::WidgetType::DigitalReadout, "Measured Speed", "drive.speed", "rad/s", 0.0, 600.0, 0.0, 0.1, 1, 0, 45, {} },
        { "current", sim_panel::WidgetType::DigitalReadout, "Motor Current", "drive.current", "A", -20.0, 20.0, 0.0, 0.01, 1, 1, 45, {} },
        { "voltage", sim_panel::WidgetType::DigitalReadout, "Motor Voltage", "drive.voltage", "V", 0.0, 24.0, 0.0, 0.01, 1, 2, 45, {} },
        { "drive_out", sim_panel::WidgetType::DigitalReadout, "Controller Output", "controller.drive_out", "V", 0.0, 24.0, 0.0, 0.01, 1, 3, 45, {} }
    };

    machine.simConfig.initialBooleanInputs["system.power"] = false;
    machine.simConfig.initialNumericInputs["system.speed_setpoint"] = 300.0;
    machineFile = folder.getChildFile("saved_motor_controller.machine.json");
    return machine.saveToFile(machineFile, error);
}

struct RunResult
{
    bool ok = false;
    juce::String error;
    double speed = 0.0;
    double current = 0.0;
    double voltage = 0.0;
    double drive = 0.0;
};

RunResult runSavedMachine(const juce::File& machineFile, bool power, bool start,
                          double setpointRadPerSec, double loadTorque, double stopTime)
{
    RunResult out;

    machine_builder::MachineDesign machine;
    juce::String error;
    if (!machine_builder::MachineDesign::loadFromFile(machineFile, machine, error))
    {
        out.error = error;
        return out;
    }

    const auto compiled = sim_panel::AssemblyCompiler::compile(machine.assembly, machineFile.getParentDirectory());
    if (!compiled.success)
    {
        out.error = compiled.error;
        return out;
    }

    circuit_sim::TransientStepper stepper(compiled.circuit);
    if (!stepper.init())
    {
        out.error = stepper.error();
        return out;
    }

    auto setSource = [&](const std::string& name, double value) {
        auto it = compiled.inputElements.find(name);
        if (it != compiled.inputElements.end())
            stepper.setSourceValue(it->second, value);
    };

    setSource("system.power", power ? 5.0 : 0.0);
    setSource("system.start", start ? 5.0 : 0.0);
    setSource("system.stop", 0.0);
    setSource("system.estop", 0.0);
    setSource("system.reset", 0.0);
    setSource("system.speed_setpoint", setpointRadPerSec * 0.01);
    setSource("drive.load_torque", loadTorque);

    if (!stepper.advance(stopTime))
    {
        out.error = stepper.error();
        return out;
    }

    out.ok = true;
    out.speed = stepper.voltage(compiled.signalNodes.at("drive.speed"));
    out.voltage = stepper.voltage(compiled.signalNodes.at("drive.voltage"));
    out.drive = stepper.voltage(compiled.signalNodes.at("controller.drive_out"));
    out.current = stepper.branchCurrent(compiled.signalBranches.at("drive.current"));
    return out;
}

juce::File outputFolder()
{
    if (const char* env = std::getenv("MODEL_DRIVEN_OUTPUT_DIR"))
        if (*env != '\0')
            return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("djehuti_model_driven_motor");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = outputFolder();

    juce::String error;
    juce::File nominalMachine;
    check(writeSavedModel(root.getChildFile("nominal"), 12.0, 0.00005, nominalMachine, error),
          "saved nominal motor/controller model files", error);

    const auto off = runSavedMachine(nominalMachine, false, true, 300.0, 0.0, 0.15);
    check(off.ok && std::abs(off.speed) < 1.0 && std::abs(off.drive) < 0.5,
          "master power OFF keeps motor unpowered",
          off.error + " speed=" + juce::String(off.speed, 4) + " drive=" + juce::String(off.drive, 4));

    const auto on = runSavedMachine(nominalMachine, true, true, 300.0, 0.0, 0.15);
    check(on.ok && on.speed > 10.0 && on.drive > 1.0,
          "saved schematic plus saved FRust controller accelerates motor",
          on.error + " speed=" + juce::String(on.speed, 4) + " current=" + juce::String(on.current, 4)
              + " voltage=" + juce::String(on.voltage, 4) + " drive=" + juce::String(on.drive, 4));

    const auto loaded = runSavedMachine(nominalMachine, true, true, 450.0, 0.0, 0.15);
    check(loaded.ok && loaded.speed > on.speed + 5.0,
          "setpoint change changes calculated speed",
          loaded.error + " nominal=" + juce::String(on.speed, 4) + " changed=" + juce::String(loaded.speed, 4));

    const auto disturbed = runSavedMachine(nominalMachine, true, true, 300.0, 0.03, 0.15);
    check(disturbed.ok && std::abs(disturbed.speed - on.speed) > 1.0,
          "load torque changes calculated transient response",
          disturbed.error + " unloaded=" + juce::String(on.speed, 4) + " loaded=" + juce::String(disturbed.speed, 4));

    juce::File lowGainMachine;
    check(writeSavedModel(root.getChildFile("low_gain"), 4.0, 0.00005, lowGainMachine, error),
          "saved modified controller-gain model", error);
    const auto lowGain = runSavedMachine(lowGainMachine, true, true, 300.0, 0.0, 0.15);
    check(lowGain.ok && std::abs(lowGain.speed - on.speed) > 5.0,
          "changing saved controller gain changes calculated response",
          lowGain.error + " kp12=" + juce::String(on.speed, 4) + " kp4=" + juce::String(lowGain.speed, 4));

    juce::File highInertiaMachine;
    check(writeSavedModel(root.getChildFile("high_inertia"), 12.0, 0.001, highInertiaMachine, error),
          "saved modified motor-inertia model", error);
    const auto highInertia = runSavedMachine(highInertiaMachine, true, true, 300.0, 0.0, 0.15);
    check(highInertia.ok && highInertia.speed < on.speed - 5.0,
          "changing saved motor inertia changes calculated response",
          highInertia.error + " nominal=" + juce::String(on.speed, 4) + " highJ=" + juce::String(highInertia.speed, 4));

    std::printf("MODEL_DIR %s\n", root.getFullPathName().toRawUTF8());
    std::printf("RESULT nominal speed=%.6f current=%.6f voltage=%.6f drive=%.6f\n",
                on.speed, on.current, on.voltage, on.drive);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

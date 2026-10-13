#include "MachineBuilderPanel.h"

#include <JuceHeader.h>

#include <cmath>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void check(bool condition, const char* label)
    {
        ++checks;
        if (condition)
            std::cout << "PASS  " << label << "\n";
        else
        {
            ++failures;
            std::cout << "FAIL  " << label << "\n";
        }
    }
};

const sim_panel::SubsystemInstance* findByKind(const machine_builder::MachineDesign& machine,
                                               sim_panel::SubsystemKind kind)
{
    for (const auto& inst : machine.assembly.instances)
        if (const auto* def = machine.assembly.findDefinition(inst.definitionId); def != nullptr && def->kind == kind)
            return &inst;
    return nullptr;
}

const sim_panel::WidgetConfig* findWidget(const machine_builder::MachineDesign& machine,
                                          sim_panel::WidgetType type)
{
    for (const auto& widget : machine.controlPanel.widgets)
        if (widget.type == type)
            return &widget;
    return nullptr;
}

double runMotor(machine_builder::MachineBuilderPanel& builder,
                sim_panel::SystemSimulationSession& session,
                const std::string& speedSignal,
                const std::string& driveSignal)
{
    if (!builder.compileAndRun())
        return 0.0;

    session.setBooleanInput("system.power", true);
    session.setBooleanInput("system.start", true);
    session.setBooleanInput("system.stop", false);
    session.setBooleanInput("system.estop", false);
    session.setBooleanInput("system.reset", false);
    session.setNumericInput("system.speed_setpoint", 300.0);

    double targetTime = session.getSnapshot().simTime;
    for (int i = 0; i < 700; ++i)
    {
        targetTime += 0.001;
        session.stepSingle(0.001);
        for (int spin = 0; spin < 100; ++spin)
        {
            if (session.getSnapshot().simTime + 1.0e-9 >= targetTime)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    const auto snap = session.getSnapshot();
    const auto speed = snap.numericSignals.count(speedSignal) != 0 ? snap.numericSignals.at(speedSignal) : 0.0;
    const auto drive = snap.numericSignals.count(driveSignal) != 0 ? snap.numericSignals.at(driveSignal) : 0.0;
    std::cout << "RESULT speed=" << speed << " drive=" << drive << "\n";
    return speed;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Runner r;

    const auto root = juce::File::getCurrentWorkingDirectory();
    const auto example = root.getChildFile("examples").getChildFile("model_driven_motor").getChildFile("nominal");
    const auto outDir = root.getChildFile("Testing").getChildFile("machine_builder_smoke");
    outDir.createDirectory();

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel builder(session, nullptr, nullptr);
    builder.setBounds(0, 0, 1200, 760);

    std::string err;
    r.check(builder.addSystemPort("power", true, sim_panel::PortDomain::SignalControl, "V", &err), "graphical builder adds system power input");
    r.check(builder.addSystemPort("start", true, sim_panel::PortDomain::SignalControl, "V", &err), "graphical builder adds system start input");
    r.check(builder.addSystemPort("stop", true, sim_panel::PortDomain::SignalControl, "V", &err), "graphical builder adds system stop input");
    r.check(builder.addSystemPort("estop", true, sim_panel::PortDomain::SignalControl, "V", &err), "graphical builder adds system estop input");
    r.check(builder.addSystemPort("reset", true, sim_panel::PortDomain::SignalControl, "V", &err), "graphical builder adds system reset input");
    r.check(builder.addSystemPort("speed_setpoint", true, sim_panel::PortDomain::SignalControl, "rad/s", &err), "graphical builder adds system setpoint input");

    r.check(builder.addSchematicSubsystemFromFile(example.getChildFile("motor_drive.diagram.json"), &err), "graphical builder adds saved schematic subsystem");
    r.check(builder.addFrustSubsystemFromFile(example.getChildFile("SavedMotorController.frcomp.json"), &err), "graphical builder adds saved FRust controller subsystem");

    const auto* drive = findByKind(builder.getMachine(), sim_panel::SubsystemKind::SchematicReference);
    const auto* controller = findByKind(builder.getMachine(), sim_panel::SubsystemKind::ProgrammableComponent);
    r.check(drive != nullptr && controller != nullptr, "builder model contains real schematic and FRust instances");

    if (drive != nullptr && controller != nullptr)
    {
        r.check(builder.connectPorts("system", "power", controller->instanceId, "pwr_in", &err), "connect system power to controller");
        r.check(builder.connectPorts("system", "start", controller->instanceId, "start_in", &err), "connect system start to controller");
        r.check(builder.connectPorts("system", "stop", controller->instanceId, "stop_in", &err), "connect system stop to controller");
        r.check(builder.connectPorts("system", "estop", controller->instanceId, "estop_in", &err), "connect system estop to controller");
        r.check(builder.connectPorts("system", "reset", controller->instanceId, "reset_in", &err), "connect system reset to controller");
        r.check(builder.connectPorts("system", "speed_setpoint", controller->instanceId, "setpoint_in", &err), "connect system setpoint to controller");
        r.check(builder.connectPorts(controller->instanceId, "drive_out", drive->instanceId, "v_in", &err), "connect controller drive to schematic motor");
        r.check(builder.connectPorts(drive->instanceId, "tach", controller->instanceId, "feedback_in", &err), "connect schematic tach to controller feedback");
    }

    const auto diags = builder.getMachine().validate(example);
    r.check(diags.empty(), "builder-created control bindings validate without warnings");
    if (!builder.getMachine().assembly.connections.empty())
        builder.getMachine().assembly.connections.front().routePoints.push_back({ 360.0, 180.0 });
    r.check(!builder.getMachine().assembly.connections.empty()
            && builder.getMachine().assembly.connections.front().routePoints.size() == 1,
            "connection routing waypoint added as visual-only model data");

    const auto savedFile = outDir.getChildFile("graphical_builder_saved.machine.json");
    juce::String ioError;
    r.check(builder.saveMachineToFile(savedFile, ioError), "builder saves .machine.json with graphical layout");

    auto sessionReloaded = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel reopened(sessionReloaded, nullptr, nullptr);
    r.check(reopened.loadMachineFromFile(savedFile, ioError), "builder reloads saved .machine.json");
    r.check(reopened.getMachine().assembly.connections.size() == builder.getMachine().assembly.connections.size(),
            "reopened machine preserves connections");
    r.check(!reopened.getMachine().assembly.connections.empty()
            && !reopened.getMachine().assembly.connections.front().connectionId.empty(),
            "connections preserve stable identities");
    r.check(!reopened.getMachine().assembly.connections.empty()
            && reopened.getMachine().assembly.connections.front().routePoints.size() == 1,
            "connection routing waypoints persist through save/load");

    const auto* reopenedDrive = findByKind(reopened.getMachine(), sim_panel::SubsystemKind::SchematicReference);
    const auto* reopenedController = findByKind(reopened.getMachine(), sim_panel::SubsystemKind::ProgrammableComponent);
    const auto speedSignal = reopenedDrive != nullptr ? reopenedDrive->instanceId + ".speed" : std::string();
    const auto driveSignal = reopenedController != nullptr ? reopenedController->instanceId + ".drive_out" : std::string();
    const auto nominalSpeed = runMotor(reopened, *sessionReloaded, speedSignal, driveSignal);
    r.check(nominalSpeed > 20.0, "reopened graphical machine executes real saved schematic and FRust controller");

    auto plotSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel plotBuilder(plotSession, nullptr, nullptr);
    plotBuilder.setMachine(reopened.getMachine());
    r.check(plotBuilder.addPlotPanel(3, &err), "builder adds passive N-channel Plot Panel instrument");
    const auto* plot = findByKind(plotBuilder.getMachine(), sim_panel::SubsystemKind::PlotPanel);
    const auto* plotDrive = findByKind(plotBuilder.getMachine(), sim_panel::SubsystemKind::SchematicReference);
    const auto* plotController = findByKind(plotBuilder.getMachine(), sim_panel::SubsystemKind::ProgrammableComponent);
    if (plot != nullptr && plotDrive != nullptr && plotController != nullptr)
    {
        r.check(plotBuilder.connectPorts(plotDrive->instanceId, "speed", plot->instanceId, "ch1", &err),
                "connect motor speed to Plot Panel channel 1");
        r.check(plotBuilder.connectPorts(plotDrive->instanceId, "tach", plot->instanceId, "ch2", &err),
                "connect tach voltage to Plot Panel channel 2");
        r.check(plotBuilder.connectPorts(plotController->instanceId, "drive_out", plot->instanceId, "ch3", &err),
                "connect controller drive voltage to Plot Panel channel 3");
    }
    const auto* plotWidget = findWidget(plotBuilder.getMachine(), sim_panel::WidgetType::PlotPanel);
    r.check(plotWidget != nullptr && plotWidget->plotChannels.size() == 3,
            "Plot Panel creates an operator-console plot widget with three channels");
    const auto expectedSpeedBinding = plotDrive != nullptr ? plotDrive->instanceId + ".speed" : std::string();
    const auto expectedDriveBinding = plotController != nullptr ? plotController->instanceId + ".drive_out" : std::string();
    r.check(plotWidget != nullptr && plotWidget->plotChannels.size() >= 3
            && plotWidget->plotChannels[0].signalName == expectedSpeedBinding
            && plotWidget->plotChannels[2].signalName == expectedDriveBinding,
            "Plot Panel channel bindings follow diagram wires");
    r.check(plotBuilder.getMachine().validate(example).empty(),
            "Plot Panel observer bindings validate without warnings");

    const auto plotSpeed = runMotor(plotBuilder, *plotSession, speedSignal, driveSignal);
    r.check(std::abs(plotSpeed - nominalSpeed) < 1.0e-6,
            "adding wired Plot Panel does not change simulated motor result");

    auto& plotMachine = plotBuilder.getMachine();
    plotMachine.controlPanel.ensureDefaultPanel();
    sim_panel::OperatorPanelConfig instrumentsPanel;
    instrumentsPanel.id = "instruments";
    instrumentsPanel.title = "Instrumentation";
    instrumentsPanel.controlColumns = 1;
    instrumentsPanel.instrumentColumns = 2;
    instrumentsPanel.splitRatio = 0.34;
    plotMachine.controlPanel.panels.push_back(instrumentsPanel);
    plotMachine.controlPanel.activePanelId = instrumentsPanel.id;
    for (auto& widget : plotMachine.controlPanel.widgets)
    {
        if (widget.type == sim_panel::WidgetType::PlotPanel)
        {
            widget.panelId = instrumentsPanel.id;
            widget.plotMode = "XY Plot";
            widget.xChannel = 0;
            widget.yChannel = 2;
        }
    }
    if (plot != nullptr)
        for (auto& inst : plotMachine.assembly.instances)
            if (inst.instanceId == plot->instanceId)
                inst.textParams["mode"] = "XY Plot";

    const auto savedPlotFile = outDir.getChildFile("graphical_builder_with_plot.machine.json");
    r.check(plotBuilder.saveMachineToFile(savedPlotFile, ioError), "builder saves Plot Panel configuration");
    auto plotReloadSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel plotReopened(plotReloadSession, nullptr, nullptr);
    r.check(plotReopened.loadMachineFromFile(savedPlotFile, ioError), "builder reloads Plot Panel configuration");
    const auto* reloadedPlotWidget = findWidget(plotReopened.getMachine(), sim_panel::WidgetType::PlotPanel);
    r.check(reloadedPlotWidget != nullptr && reloadedPlotWidget->plotMode == "XY Plot"
            && reloadedPlotWidget->plotChannels.size() == 3
            && reloadedPlotWidget->plotChannels[0].signalName == expectedSpeedBinding,
            "Plot Panel mode and channel bindings survive save/load");
    r.check(plotReopened.getMachine().controlPanel.activePanelId == instrumentsPanel.id
            && plotReopened.getMachine().controlPanel.findPanel(instrumentsPanel.id) != nullptr
            && reloadedPlotWidget != nullptr && reloadedPlotWidget->panelId == instrumentsPanel.id,
            "operator panel definitions and Plot Panel assignment survive save/load");

    if (reopenedController != nullptr)
        reopened.getMachine().setInstanceParameter(reopenedController->instanceId, "kp", 4.0, nullptr);
    auto lowGainSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel lowGain(lowGainSession, nullptr, nullptr);
    lowGain.setMachine(reopened.getMachine());
    const auto lowGainSpeed = runMotor(lowGain, *lowGainSession, speedSignal, driveSignal);
    r.check(std::abs(nominalSpeed - lowGainSpeed) > 1.0, "editing controller gain through builder changes simulated result");

    if (reopenedDrive != nullptr)
        reopened.getMachine().setInstanceParameter(reopenedDrive->instanceId, "j", 0.001, nullptr);
    auto highInertiaSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel highInertia(highInertiaSession, nullptr, nullptr);
    highInertia.setMachine(reopened.getMachine());
    const auto highInertiaSpeed = runMotor(highInertia, *highInertiaSession, speedSignal, driveSignal);
    r.check(std::abs(nominalSpeed - highInertiaSpeed) > 1.0, "editing motor inertia through builder changes simulated result");

    auto directSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel directDrive(directSession, nullptr, nullptr);
    r.check(directDrive.addSystemPort("drive_voltage", true, sim_panel::PortDomain::Electrical, "V", &err), "second assembly adds direct voltage input");
    r.check(directDrive.addBuiltInMotor(&err), "second assembly adds built-in motor without machine-specific C++");
    const auto* builtInMotor = findByKind(directDrive.getMachine(), sim_panel::SubsystemKind::DCMotor);
    if (builtInMotor != nullptr)
        r.check(directDrive.connectPorts("system", "drive_voltage", builtInMotor->instanceId, "v_in", &err),
                "second assembly wires system voltage to built-in motor");
    r.check(directDrive.compileAndRun(), "second materially different assembly compiles through same builder");

    auto resetSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel resetBuilder(resetSession, nullptr, nullptr);
    resetBuilder.setMachine(builder.getMachine());
    r.check(resetBuilder.compileAndRun(), "builder compiles before New Blank reset");
    r.check(resetBuilder.startSimulation(), "builder starts simulation before New Blank reset");
    resetBuilder.loadTemplate("blank");
    r.check(resetBuilder.getMachine().assembly.instances.empty()
            && resetBuilder.getMachine().assembly.connections.empty()
            && resetBuilder.getMachine().controlPanel.widgets.empty(),
            "New Blank clears machine graph and control layout immediately");
    r.check(!resetSession->getSnapshot().running, "New Blank stops the active simulation session");

    std::cout << r.checks << " checks, " << r.failures << " failures\n";
    return r.failures == 0 ? 0 : 1;
}

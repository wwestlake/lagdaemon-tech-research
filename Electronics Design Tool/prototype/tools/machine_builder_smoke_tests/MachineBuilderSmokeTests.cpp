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

    // Compact editing toolbar organization, tooltips, canvas workspace, and overflow handling
    builder.setBounds(0, 0, 1200, 760);
    const auto groups = builder.getToolbarGroupSummaries();
    r.check(groups.size() == 5
            && groups[0].name == "File"
            && groups[1].name == "Diagram"
            && groups[2].name == "View"
            && groups[3].name == "Simulation"
            && groups[4].name == "Tools",
            "toolbar organizes commands into File, Diagram, View, Simulation, and Tools groups");

    bool allTooltipsPresent = true;
    size_t totalCommands = 0;
    for (const auto& grp : groups)
    {
        totalCommands += grp.commandIds.size();
        for (const auto& cmd : grp.commandIds)
            if (builder.getToolbarCommandTooltip(cmd).isEmpty())
                allTooltipsPresent = false;
    }
    r.check(totalCommands == 21 && allTooltipsPresent,
            "all 21 toolbar commands have informative tooltips");

    r.check(builder.getVisibleToolbarButtonCount() == 21
            && builder.getOverflowToolbarButtonCount() == 0
            && !builder.isOverflowButtonVisible(),
            "all toolbar commands are visible without overflow at standard 1200x760 window size");

    const auto tbBounds = builder.getToolbarBounds();
    const auto canvasBounds = builder.getCanvasBounds();
    r.check(tbBounds.getHeight() <= 40 && canvasBounds.getWidth() >= 940 && canvasBounds.getHeight() >= 700,
            "compact top toolbar maximizes usable diagram canvas workspace");

    builder.setBounds(0, 0, 460, 600);
    r.check(builder.getVisibleToolbarButtonCount() > 0
            && builder.getOverflowToolbarButtonCount() > 0
            && builder.isOverflowButtonVisible(),
            "narrow window activates toolbar overflow button while keeping primary commands accessible");

    builder.setBounds(0, 0, 1200, 760);
    r.check(builder.getVisibleToolbarButtonCount() == 21
            && builder.getOverflowToolbarButtonCount() == 0
            && !builder.isOverflowButtonVisible(),
            "restoring wide window restores full toolbar visibility and hides overflow button");

    r.check(builder.triggerToolbarCommand("zoom_in")
            && builder.triggerToolbarCommand("zoom_out")
            && builder.triggerToolbarCommand("reset_view")
            && builder.triggerToolbarCommand("fit_all")
            && builder.triggerToolbarCommand("validate"),
            "toolbar view and validation commands execute cleanly");

    // =========================================================================
    // Control Panel & Multichannel Strip-Chart Recorder Verification
    // =========================================================================
    auto cpSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel cpBuilder(cpSession, nullptr, nullptr);
    cpBuilder.setMachine(reopened.getMachine());
    r.check(cpBuilder.addPlotPanel(4, &err), "builder adds 4-channel strip-chart Plot Panel");

    const auto* cpPlot = findByKind(cpBuilder.getMachine(), sim_panel::SubsystemKind::PlotPanel);
    const auto* cpDrive = findByKind(cpBuilder.getMachine(), sim_panel::SubsystemKind::SchematicReference);
    const auto* cpCtrl = findByKind(cpBuilder.getMachine(), sim_panel::SubsystemKind::ProgrammableComponent);
    if (cpPlot != nullptr && cpDrive != nullptr && cpCtrl != nullptr)
    {
        r.check(cpBuilder.connectPorts(cpDrive->instanceId, "speed", cpPlot->instanceId, "ch1", &err),
                "wire motor speed to strip-chart channel 1");
        r.check(cpBuilder.connectPorts(cpDrive->instanceId, "tach", cpPlot->instanceId, "ch2", &err),
                "wire motor tach voltage to strip-chart channel 2");
        r.check(cpBuilder.connectPorts(cpCtrl->instanceId, "drive_out", cpPlot->instanceId, "ch3", &err),
                "wire controller drive_out to strip-chart channel 3");
        r.check(cpBuilder.connectPorts(cpCtrl->instanceId, "fault_lamp", cpPlot->instanceId, "ch4", &err),
                "wire zero-valued fault_lamp to strip-chart channel 4");
    }

    sim_panel::ControlPanelComponent cpComp(cpSession);
    cpComp.setBounds(0, 0, 1280, 860);
    cpComp.setLayout(cpBuilder.getMachine().controlPanel);

    // 1. New Panel creation, selection, renaming, and removal without freezing
    const int initialPanels = cpComp.getPanelCount();
    const auto panel2Id = cpComp.createNewPanel("Test Panel 2");
    const auto panel3Id = cpComp.createNewPanel("Test Panel 3");
    r.check(!panel2Id.empty() && !panel3Id.empty() && panel2Id != panel3Id
            && cpComp.getPanelCount() == initialPanels + 2,
            "New Panel creates distinct panel IDs and increments panel count without freezing");
    cpComp.setActivePanel(panel2Id);
    r.check(cpComp.getActivePanelId() == panel2Id,
            "selecting an operator panel switches active panel");
    r.check(cpComp.renameActivePanel("Diagnostics Console")
            && cpComp.getLayout().findPanel(panel2Id) != nullptr
            && cpComp.getLayout().findPanel(panel2Id)->title == "Diagnostics Console",
            "renaming active panel updates panel title cleanly");
    r.check(cpComp.removeActivePanel() && cpComp.getPanelCount() == initialPanels + 1
            && !cpComp.getLayout().hasPanel(panel2Id),
            "removing active panel deletes it and selects a valid remaining panel");
    cpComp.setActivePanel(panel3Id);
    r.check(cpComp.removeActivePanel() && cpComp.getPanelCount() == initialPanels,
            "removing third panel restores original panel count");

    // 2. Signal acquisition, 4 stacked lanes, and fixed timebase from first sample
    const auto* cpPlotWidget = findWidget(cpBuilder.getMachine(), sim_panel::WidgetType::PlotPanel);
    const std::string plotWidgetId = cpPlotWidget != nullptr ? cpPlotWidget->id : std::string();
    r.check(!plotWidgetId.empty(), "ControlPanelComponent hosts the 4-channel PlotPanel widget");

    r.check(cpBuilder.compileAndRun(), "4-channel strip-chart machine compiles cleanly");
    r.check(cpComp.setRecorderTimebaseSecondsPerDiv(plotWidgetId, 0.05),
            "operator sets horizontal timebase to 50 ms/div (0.5 s visible window)");

    cpSession->setBooleanInput("system.power", true);
    cpSession->setBooleanInput("system.start", true);
    cpSession->setBooleanInput("system.stop", false);
    cpSession->setBooleanInput("system.estop", false);
    cpSession->setBooleanInput("system.reset", false);
    cpSession->setNumericInput("system.speed_setpoint", 300.0);

    auto stepAndSample = [&](int steps, int sampleEvery)
    {
        double targetTime = cpSession->getSnapshot().simTime;
        for (int i = 1; i <= steps; ++i)
        {
            targetTime += 0.001;
            cpSession->stepSingle(0.001);
            for (int spin = 0; spin < 100; ++spin)
            {
                if (cpSession->getSnapshot().simTime + 1.0e-9 >= targetTime)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (i % sampleEvery == 0 || i == steps)
                cpComp.sampleLatestNow();
        }
    };

    stepAndSample(5, 1);
    auto recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty()
            && recSnaps.front().totalSamples >= 1
            && std::abs(recSnaps.front().secondsPerDivision - 0.05) < 1.0e-6
            && std::abs(recSnaps.front().visibleWindowSeconds - 0.5) < 1.0e-6
            && std::abs((recSnaps.front().viewEndTime - recSnaps.front().viewStartTime) - 0.5) < 1.0e-6,
            "selected horizontal timebase applies immediately from the first sample without compression");

    // Advance past the 0.5 s window so continuous scrolling engages
    stepAndSample(650, 5);
    const double latestSimTime = cpSession->getSnapshot().simTime;
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty() && recSnaps.front().lanes.size() == 4,
            "strip-chart recorder renders 4 independent channel lanes");

    if (!recSnaps.empty() && recSnaps.front().lanes.size() == 4)
    {
        const auto& snap = recSnaps.front();
        const auto& l0 = snap.lanes[0];
        const auto& l1 = snap.lanes[1];
        const auto& l2 = snap.lanes[2];
        const auto& l3 = snap.lanes[3];

        r.check(l0.signalAvailable && l1.signalAvailable && l2.signalAvailable && l3.signalAvailable
                && l0.finiteSampleCount > 50 && l1.finiteSampleCount > 50
                && l2.finiteSampleCount > 50 && l3.finiteSampleCount > 50,
                "all 4 connected channels acquire valid finite samples from the simulation");

        r.check(l0.latestValue > 20.0 && l1.latestValue > 0.05 && l2.latestValue > 0.5,
                "motor speed, schematic tach port, and FRust controller drive_out produce real non-zero waveforms");

        r.check(std::abs(l3.latestValue) < 1.0e-9 && l3.yMin < 0.0 && l3.yMax > 0.0,
                "valid zero-valued signal (fault_lamp = 0) has centered vertical scale and renders as flat trace");

        r.check(l0.boundsInRecorder.getHeight() >= 150 && l0.boundsInRecorder.getHeight() <= 200
                && l0.boundsInRecorder.getBottom() <= l1.boundsInRecorder.getY()
                && l1.boundsInRecorder.getBottom() <= l2.boundsInRecorder.getY()
                && l2.boundsInRecorder.getBottom() <= l3.boundsInRecorder.getY(),
                "4 channels are vertically stacked in independent non-overlapping DAW-style lanes (~165 px each)");

        r.check(std::abs(l0.yMax - l1.yMax) > 1.0,
                "each channel lane scales its vertical axis independently");

        // Continuous scrolling once visible window fills
        r.check(snap.liveFollow
                && latestSimTime > 0.55
                && snap.viewStartTime > 0.05
                && std::abs((snap.viewEndTime - snap.viewStartTime) - 0.5) < 1.0e-6
                && std::abs(snap.viewEndTime - latestSimTime) < 1.0e-2,
                "once visible time window fills, strip chart scrolls continuously at fixed timebase");
    }

    // 3. Lane height adjustment and individual channel collapse/expand
    r.check(cpComp.setRecorderLaneHeight(plotWidgetId, 0, 210)
            && cpComp.setRecorderLaneCollapsed(plotWidgetId, 1, true),
            "operator adjusts lane height and collapses individual channel lane");
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty() && recSnaps.front().lanes.size() == 4
            && recSnaps.front().lanes[0].boundsInRecorder.getHeight() == 210
            && recSnaps.front().lanes[1].collapsed
            && recSnaps.front().lanes[1].boundsInRecorder.getHeight() <= 36,
            "lane height resize and collapse update strip-chart lane geometry");
    r.check(cpComp.setRecorderLaneCollapsed(plotWidgetId, 1, false),
            "operator re-expands collapsed channel lane");

    // 4. Historical navigation, zoom around current view, Pause View vs Record, and Stopped review
    r.check(cpComp.scrollRecorderHistory(plotWidgetId, 0.25),
            "operator scrolls backward into recorded history");
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    const double scrolledCenter = !recSnaps.empty()
        ? 0.5 * (recSnaps.front().viewStartTime + recSnaps.front().viewEndTime)
        : 0.0;
    const int countBeforeZoom = !recSnaps.empty() ? recSnaps.front().totalSamples : 0;
    r.check(!recSnaps.empty()
            && !recSnaps.front().liveFollow
            && recSnaps.front().viewEndTime < latestSimTime - 0.10,
            "scrolling into history automatically disengages Live/Follow");

    r.check(cpComp.setRecorderTimebaseSecondsPerDiv(plotWidgetId, 0.02),
            "operator changes timebase to 20 ms/div while reviewing history");
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    const double zoomedCenter = !recSnaps.empty()
        ? 0.5 * (recSnaps.front().viewStartTime + recSnaps.front().viewEndTime)
        : 0.0;
    r.check(!recSnaps.empty()
            && !recSnaps.front().liveFollow
            && std::abs((recSnaps.front().viewEndTime - recSnaps.front().viewStartTime) - 0.2) < 1.0e-6
            && std::abs(zoomedCenter - scrolledCenter) < 0.05
            && recSnaps.front().totalSamples == countBeforeZoom,
            "changing timebase in history zooms around current view center without jumping to latest or losing samples");

    r.check(cpComp.setRecorderLiveFollow(plotWidgetId, true),
            "Live/Follow control returns view to newest data");
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty()
            && recSnaps.front().liveFollow
            && std::abs(recSnaps.front().viewEndTime - latestSimTime) < 1.0e-2,
            "re-engaging Live/Follow aligns view end with newest sample");

    // Pausing view must not pause simulation or recording
    r.check(cpComp.setRecorderViewPaused(plotWidgetId, true), "operator pauses display view");
    const double pausedViewEnd = recSnaps.front().viewEndTime;
    const int countBeforeViewPauseStep = recSnaps.front().totalSamples;
    stepAndSample(40, 2);
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty()
            && recSnaps.front().viewPaused
            && recSnaps.front().totalSamples > countBeforeViewPauseStep
            && std::abs(recSnaps.front().viewEndTime - pausedViewEnd) < 1.0e-6,
            "pausing view keeps display window fixed while simulation and recording continue");
    cpComp.setRecorderViewPaused(plotWidgetId, false);
    cpComp.setRecorderLiveFollow(plotWidgetId, true);

    // Stopping simulation preserves recorded data and allows historical navigation & export
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    const int countBeforeStop = !recSnaps.empty() ? recSnaps.front().totalSamples : 0;
    cpSession->stop();
    cpComp.sampleLatestNow();
    r.check(cpComp.scrollRecorderHistory(plotWidgetId, 0.30)
            && cpComp.setRecorderTimebaseSecondsPerDiv(plotWidgetId, 0.05),
            "operator scrolls and changes timebase after simulation is stopped");
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty() && recSnaps.front().totalSamples == countBeforeStop,
            "stopping simulation preserves recorded history for inspection");

    const auto csvFile = outDir.getChildFile("strip_chart_export.csv");
    csvFile.deleteFile();
    r.check(cpComp.exportRecorderCsv(plotWidgetId, csvFile, ioError)
            && csvFile.existsAsFile()
            && csvFile.loadFileAsString().contains("time_s"),
            "strip-chart recorder exports recorded history to CSV");

    std::cout << r.checks << " checks, " << r.failures << " failures\n";
    return r.failures == 0 ? 0 : 1;
}

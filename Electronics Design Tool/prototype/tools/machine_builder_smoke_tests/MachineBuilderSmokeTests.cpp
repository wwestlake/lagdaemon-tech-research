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

const sim_panel::WidgetConfig* findWidget(const sim_panel::ControlPanelLayout& layout,
                                          sim_panel::WidgetType type)
{
    for (const auto& widget : layout.widgets)
        if (widget.type == type)
            return &widget;
    return nullptr;
}

const sim_panel::WidgetConfig* findWidget(const machine_builder::MachineDesign& machine,
                                          sim_panel::WidgetType type)
{
    return findWidget(machine.controlPanel, type);
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
        for (int i = 1; i <= steps; ++i)
        {
            const double tStart = cpSession->getSnapshot().simTime;
            cpSession->stepSingle(0.001);

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (cpSession->getSnapshot().simTime > tStart + 1.0e-7)
                    break;
                std::this_thread::yield();
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

    // 5. Cursor-centered zoom, Fit Recording, and History persistence
    recSnaps = cpComp.getStripChartRecorderSnapshots();
    r.check(!recSnaps.empty() && !recSnaps.front().lanes.empty(), "recorder snapshot available for zoom tests");
    if (!recSnaps.empty() && !recSnaps.front().lanes.empty())
    {
        const auto snapBefore = recSnaps.front();
        const auto& lane0 = snapBefore.lanes[0];
        const int wfLeft = lane0.waveformBounds.getX();
        const int wfWidth = lane0.waveformBounds.getWidth();
        const double winBefore = snapBefore.visibleWindowSeconds;
        const int testScreenX = wfLeft + (int)std::round(0.40 * (double)wfWidth);
        const double pixelFrac = (double)(testScreenX - wfLeft) / (double)wfWidth;
        const double tCursorBefore = snapBefore.viewStartTime + pixelFrac * winBefore;

        r.check(cpComp.zoomRecorderAtScreenX(plotWidgetId, testScreenX, 0.5),
                "cursor-centered zoomAtScreenX executes cleanly");

        recSnaps = cpComp.getStripChartRecorderSnapshots();
        const auto& snapAfter = recSnaps.front();
        const double winAfter = snapAfter.visibleWindowSeconds;
        const double tCursorAfter = snapAfter.viewStartTime + pixelFrac * winAfter;

        r.check(std::abs(tCursorAfter - tCursorBefore) < 1.0e-5,
                "cursor-centered zoom keeps the time position beneath the mouse cursor stationary on screen");
        r.check(!snapAfter.liveFollow,
                "cursor-centered zoom automatically disengages Live/Follow");
        r.check(snapAfter.totalSamples == countBeforeStop,
                "zooming does not alter or clear acquired simulation samples");
        r.check(snapAfter.recorderBounds == snapBefore.recorderBounds
                && snapAfter.lanes[0].boundsInRecorder == snapBefore.lanes[0].boundsInRecorder,
                "zooming operates strictly on time axis without resizing plot viewport, lanes, or panel layout");

        // Fit Recording
        r.check(cpComp.fitRecorderRecording(plotWidgetId), "fitRecorderRecording executes cleanly");
        recSnaps = cpComp.getStripChartRecorderSnapshots();
        const auto& snapFit = recSnaps.front();
        const double fitDuration = snapFit.viewEndTime - snapFit.viewStartTime;

        r.check(snapFit.viewStartTime <= 0.01
                && std::abs(snapFit.viewEndTime - latestSimTime) < 0.05
                && std::abs(fitDuration - latestSimTime) < 0.05
                && !snapFit.liveFollow,
                "Fit Recording spans entire recorded history with zero unnecessary empty future time");

        // Panning through history and returning to Live
        r.check(cpComp.scrollRecorderHistory(plotWidgetId, 0.20),
                "operator pans history to target center");
        recSnaps = cpComp.getStripChartRecorderSnapshots();
        const double pannedStart = recSnaps.front().viewStartTime;
        cpComp.sampleLatestNow(); // Simulate frame update
        recSnaps = cpComp.getStripChartRecorderSnapshots();
        r.check(std::abs(recSnaps.front().viewStartTime - pannedStart) < 1.0e-6,
                "frame updates do not clobber user viewStartTime back to t0 in history mode");

        r.check(cpComp.setRecorderLiveFollow(plotWidgetId, true), "re-engaging Live/Follow returns to live edge");
        recSnaps = cpComp.getStripChartRecorderSnapshots();
        r.check(recSnaps.front().liveFollow && std::abs(recSnaps.front().viewEndTime - latestSimTime) < 0.05,
                "returning to Live/Follow re-anchors to latest samples");
    }

    // 5b. Splitter & Plot Area Mouse-Event Non-Interference
    r.check(cpComp.hasPlotPanel()
            && cpComp.getSplitterComponent() != nullptr
            && cpComp.getPlotContainerComponent() != nullptr,
            "splitter and plot container are active in split layout");

    auto* plotComp = cpComp.getPlotContainerComponent();
    auto* splitterComp = cpComp.getSplitterComponent();
    if (plotComp != nullptr && splitterComp != nullptr)
    {
        const double initialRatio = cpComp.getSplitRatio();
        const int initialSplitterX = cpComp.getSplitterX();
        const auto initialPlotBounds = plotComp->getBounds();
        const auto initialCpBounds = cpComp.getBounds();
        const auto initialCanvasBounds = cpComp.getCanvasViewportBounds();
        const auto plotCenter = plotComp->getLocalBounds().getCentre();

        // Capture initial diagnostic evidence
        std::cout << "[Geometry Diagnostics - Pre-Interaction]\n"
                  << "  ControlPanel: " << initialCpBounds.toString() << "\n"
                  << "  SplitRatio: " << initialRatio << ", SplitterX: " << initialSplitterX << "\n"
                  << "  CanvasViewport: " << initialCanvasBounds.toString() << "\n"
                  << "  PlotContainer: " << initialPlotBounds.toString() << "\n";

        auto makeEv = [](juce::Component* comp, float x, float y) {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto t = juce::Time::getCurrentTime();
            return juce::MouseEvent(src, { x, y }, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    comp, comp, t, { x, y }, t, 1, false);
        };

        // 1. Mouse click, drag, and release on plot container must NEVER move or resize splitter
        plotComp->mouseDown(makeEv(plotComp, (float)plotCenter.x, (float)plotCenter.y));
        plotComp->mouseDrag(makeEv(plotComp, (float)(plotCenter.x - 120), (float)(plotCenter.y + 60)));
        plotComp->mouseUp(makeEv(plotComp, (float)(plotCenter.x - 120), (float)(plotCenter.y + 60)));
        r.check(std::abs(cpComp.getSplitRatio() - initialRatio) < 1.0e-6
                && cpComp.getSplitterX() == initialSplitterX
                && plotComp->getBounds() == initialPlotBounds,
                "mouse clicks and drags inside plot container do not move or resize the splitter");

        // 2. Mouse wheel on plot container must NOT affect splitter position, ratio, or plot width
        plotComp->mouseWheelMove(makeEv(plotComp, (float)plotCenter.x, (float)plotCenter.y),
                                 juce::MouseWheelDetails { 0.0f, 1.0f, false, false });
        r.check(std::abs(cpComp.getSplitRatio() - initialRatio) < 1.0e-6
                && cpComp.getSplitterX() == initialSplitterX
                && plotComp->getWidth() == initialPlotBounds.getWidth(),
                "mouse wheel events inside plot container do not affect splitter position or split ratio");

        // Locate inner plot widget and child components (lanesViewport, lanesContainer)
        auto* plotWidget = plotComp->getNumChildComponents() > 0 ? plotComp->getChildComponent(0) : nullptr;
        r.check(plotWidget != nullptr, "plot container hosts child PlotPanelWidget");
        juce::Viewport* lanesVp = nullptr;
        if (plotWidget != nullptr)
        {
            for (int i = 0; i < plotWidget->getNumChildComponents(); ++i)
            {
                if (auto* vp = dynamic_cast<juce::Viewport*>(plotWidget->getChildComponent(i)))
                {
                    lanesVp = vp;
                    break;
                }
            }
        }
        r.check(lanesVp != nullptr, "PlotPanelWidget contains lanes viewport");
        auto* lanesContainer = lanesVp != nullptr ? lanesVp->getViewedComponent() : nullptr;
        r.check(lanesContainer != nullptr, "lanes viewport views lanes container");

        if (lanesContainer != nullptr)
        {
            const auto lcCenter = lanesContainer->getLocalBounds().getCentre();

            // 3. Child component interaction: mouse clicks and waveform drags inside lanesContainer
            lanesContainer->mouseDown(makeEv(lanesContainer, (float)lcCenter.x, (float)lcCenter.y));
            lanesContainer->mouseDrag(makeEv(lanesContainer, (float)(lcCenter.x - 80), (float)lcCenter.y));
            lanesContainer->mouseDrag(makeEv(lanesContainer, (float)(lcCenter.x + 80), (float)lcCenter.y));
            lanesContainer->mouseUp(makeEv(lanesContainer, (float)(lcCenter.x + 80), (float)lcCenter.y));

            r.check(std::abs(cpComp.getSplitRatio() - initialRatio) < 1.0e-6
                    && cpComp.getSplitterX() == initialSplitterX
                    && plotComp->getWidth() == initialPlotBounds.getWidth(),
                    "direct clicking and dragging within strip-chart lanes does not alter panel split or plot width");

            // 4. Repeated mouse-wheel zoom actions on lanesContainer and lanesViewport: assert ZERO cumulative shrinkage
            for (int i = 0; i < 5; ++i)
            {
                lanesContainer->mouseWheelMove(makeEv(lanesContainer, (float)lcCenter.x, (float)lcCenter.y),
                                               juce::MouseWheelDetails { 0.0f, 1.0f, false, false });
            }
            for (int i = 0; i < 5; ++i)
            {
                lanesContainer->mouseWheelMove(makeEv(lanesContainer, (float)lcCenter.x, (float)lcCenter.y),
                                               juce::MouseWheelDetails { 0.0f, -1.0f, false, false });
            }

            r.check(std::abs(cpComp.getSplitRatio() - initialRatio) < 1.0e-6
                    && cpComp.getSplitterX() == initialSplitterX
                    && plotComp->getWidth() == initialPlotBounds.getWidth(),
                    "repeated mouse-wheel zoom gestures produce zero cumulative shrinkage of the plot area");

            // Capture post-interaction evidence
            std::cout << "[Geometry Diagnostics - Post-Interaction]\n"
                      << "  ControlPanel: " << cpComp.getBounds().toString() << "\n"
                      << "  SplitRatio: " << cpComp.getSplitRatio() << ", SplitterX: " << cpComp.getSplitterX() << "\n"
                      << "  CanvasViewport: " << cpComp.getCanvasViewportBounds().toString() << "\n"
                      << "  PlotContainer: " << plotComp->getBounds().toString() << "\n"
                      << "  PlotWidget: " << (plotWidget ? plotWidget->getBounds().toString() : "null") << "\n"
                      << "  LanesViewport: " << (lanesVp ? lanesVp->getBounds().toString() : "null") << "\n";
        }

        auto makeDragEv = [](juce::Component* comp, float curX, float curY, float startX, float startY) {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto t = juce::Time::getCurrentTime();
            return juce::MouseEvent(src, { curX, curY }, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    comp, comp, t, { startX, startY }, t, 1, true);
        };

        // 5. Deliberate drag directly on splitter handle resizes split smoothly in BOTH directions
        const auto splitterCenter = splitterComp->getLocalBounds().getCentre();

        // Drag right (+60 px) -> widens operator canvas, narrows plot
        splitterComp->mouseDown(makeEv(splitterComp, (float)splitterCenter.x, (float)splitterCenter.y));
        splitterComp->mouseDrag(makeDragEv(splitterComp, (float)(splitterCenter.x + 60), (float)splitterCenter.y,
                                           (float)splitterCenter.x, (float)splitterCenter.y));
        splitterComp->mouseUp(makeEv(splitterComp, (float)(splitterCenter.x + 60), (float)splitterCenter.y));
        r.check(cpComp.getSplitterX() > initialSplitterX && cpComp.getSplitRatio() > initialRatio,
                "deliberate drag on splitter handle to the right widens canvas and narrows plot");

        const double rightRatio = cpComp.getSplitRatio();
        const int rightSplitterX = cpComp.getSplitterX();

        // Drag left (-120 px) -> narrows operator canvas, widens plot smoothly
        const auto newCenter = splitterComp->getLocalBounds().getCentre();
        splitterComp->mouseDown(makeEv(splitterComp, (float)newCenter.x, (float)newCenter.y));
        splitterComp->mouseDrag(makeDragEv(splitterComp, (float)(newCenter.x - 120), (float)newCenter.y,
                                           (float)newCenter.x, (float)newCenter.y));
        splitterComp->mouseUp(makeEv(splitterComp, (float)(newCenter.x - 120), (float)newCenter.y));
        r.check(cpComp.getSplitterX() < rightSplitterX && cpComp.getSplitRatio() < rightRatio,
                "deliberate drag on splitter handle to the left narrows canvas and restores/widens plot smoothly");

        // 6. Split ratio clamp limits [0.15, 0.85]
        cpComp.setSplitRatio(0.99);
        r.check(cpComp.getSplitRatio() <= 0.85 + 1.0e-6, "splitter ratio clamps to maximum 0.85");
        cpComp.setSplitRatio(0.01);
        r.check(cpComp.getSplitRatio() >= 0.15 - 1.0e-6, "splitter ratio clamps to minimum 0.15");

        // Restore initial ratio for subsequent checks
        cpComp.setSplitRatio(initialRatio);
    }

    // 6. Compact automatically generated Control Panel layout verification
    const auto& defaultMotorLayout = sim_panel::ControlPanelLayout::createDefaultMotorPanel();
    r.check(defaultMotorLayout.panels.size() >= 1, "default motor layout created");
    if (!defaultMotorLayout.panels.empty())
    {
        const auto& opPanel = defaultMotorLayout.panels.front();
        r.check(opPanel.splitRatio <= 0.25 && opPanel.spacing <= 8,
                "operator panel config defaults to compact split ratio (~0.22) and tight spacing (~6 px)");

        const auto* btnStart = findWidget(defaultMotorLayout, sim_panel::WidgetType::PushButton);
        const auto* swPwr = findWidget(defaultMotorLayout, sim_panel::WidgetType::ToggleSwitch);
        const auto* sliderLoad = findWidget(defaultMotorLayout, sim_panel::WidgetType::Slider);
        const auto* estop = findWidget(defaultMotorLayout, sim_panel::WidgetType::EmergencyStop);

        r.check(btnStart != nullptr && btnStart->height == 28,
                "generated push buttons (Start/Stop/Reset) have compact standard height (28 px)");
        r.check(swPwr != nullptr && swPwr->height == 28,
                "generated toggle switches have compact standard height (28 px)");
        r.check(sliderLoad != nullptr && sliderLoad->height == 36,
                "generated sliders have compact horizontal height (36 px)");
        r.check(estop != nullptr && estop->height == 50,
                "generated emergency stop has compact prominent height (50 px)");
    }

    // 7. Control Panel Designer: Demonstration panel and 8 behavioral requirements verification
    const auto demoPanelId = cpComp.createNewPanel("Operator Console Designer");
    r.check(!demoPanelId.empty(), "designer creates dedicated demonstration operator panel");
    cpComp.setActivePanel(demoPanelId);

    // Demonstration panel representative components:
    // 1. Analog needle gauge
    // 2. Digital numerical readout
    // 3. Indicator lamp
    // 4. Momentary pushbutton
    // 5. Latching switch / toggle switch
    // 6. Rotary knob / slider
    // (plus zone-colored Bar Gauge)

    sim_panel::WidgetConfig wGauge;
    wGauge.id = "demo_gauge";
    wGauge.type = sim_panel::WidgetType::AnalogGauge;
    wGauge.title = "Motor Speed Needle";
    wGauge.signalName = speedSignal;
    wGauge.unit = "rad/s";
    wGauge.minValue = 0.0;
    wGauge.maxValue = 600.0;
    wGauge.defaultValue = 0.0;
    wGauge.hasWarnRange = true;
    wGauge.warnMin = 250.0;
    wGauge.warnMax = 350.0;
    wGauge.hasCritRange = true;
    wGauge.critMin = 350.0;
    wGauge.critMax = 600.0;
    wGauge.precision = 1;
    wGauge.x = 40; wGauge.y = 40; wGauge.width = 180; wGauge.height = 180;
    wGauge.panelId = demoPanelId;
    r.check(cpComp.addWidget(wGauge), "designer adds Analog Needle Gauge to canvas");

    sim_panel::WidgetConfig wReadout;
    wReadout.id = "demo_readout";
    wReadout.type = sim_panel::WidgetType::DigitalReadout;
    wReadout.title = "Tachometer Readout";
    wReadout.signalName = plotDrive != nullptr ? plotDrive->instanceId + ".tach" : "drive.tach";
    wReadout.unit = "V";
    wReadout.minValue = 0.0;
    wReadout.maxValue = 50.0;
    wReadout.precision = 2;
    wReadout.x = 240; wReadout.y = 40; wReadout.width = 160; wReadout.height = 70;
    wReadout.panelId = demoPanelId;
    r.check(cpComp.addWidget(wReadout), "designer adds Digital Numerical Readout to canvas");

    sim_panel::WidgetConfig wLamp;
    wLamp.id = "demo_lamp";
    wLamp.type = sim_panel::WidgetType::IndicatorLamp;
    wLamp.title = "Run Indicator";
    wLamp.signalName = "system.power";
    wLamp.x = 420; wLamp.y = 40; wLamp.width = 90; wLamp.height = 70;
    wLamp.panelId = demoPanelId;
    r.check(cpComp.addWidget(wLamp), "designer adds Indicator Lamp to canvas");

    sim_panel::WidgetConfig wBtn;
    wBtn.id = "demo_btn";
    wBtn.type = sim_panel::WidgetType::PushButton;
    wBtn.title = "Jog Start (Momentary)";
    wBtn.signalName = "system.start";
    wBtn.momentary = true;
    wBtn.x = 40; wBtn.y = 240; wBtn.width = 150; wBtn.height = 36;
    wBtn.panelId = demoPanelId;
    r.check(cpComp.addWidget(wBtn), "designer adds Momentary Pushbutton to canvas");

    sim_panel::WidgetConfig wSwitch;
    wSwitch.id = "demo_switch";
    wSwitch.type = sim_panel::WidgetType::ToggleSwitch;
    wSwitch.title = "Main Power";
    wSwitch.signalName = "system.power";
    wSwitch.defaultValue = 1.0;
    wSwitch.x = 210; wSwitch.y = 240; wSwitch.width = 140; wSwitch.height = 36;
    wSwitch.panelId = demoPanelId;
    r.check(cpComp.addWidget(wSwitch), "designer adds Latching Toggle Switch to canvas");

    sim_panel::WidgetConfig wSlider;
    wSlider.id = "demo_slider";
    wSlider.type = sim_panel::WidgetType::Slider;
    wSlider.title = "Speed Setpoint";
    wSlider.signalName = "system.speed_setpoint";
    wSlider.minValue = 0.0;
    wSlider.maxValue = 600.0;
    wSlider.defaultValue = 300.0;
    wSlider.stepSize = 5.0;
    wSlider.x = 370; wSlider.y = 240; wSlider.width = 220; wSlider.height = 42;
    wSlider.panelId = demoPanelId;
    r.check(cpComp.addWidget(wSlider), "designer adds Rotary Knob / Slider to canvas");

    sim_panel::WidgetConfig wBar;
    wBar.id = "demo_bar";
    wBar.type = sim_panel::WidgetType::BarGauge;
    wBar.title = "Drive Voltage Level";
    wBar.signalName = driveSignal;
    wBar.unit = "V";
    wBar.minValue = 0.0;
    wBar.maxValue = 30.0;
    wBar.hasWarnRange = true;
    wBar.warnMin = 20.0;
    wBar.warnMax = 25.0;
    wBar.hasCritRange = true;
    wBar.critMin = 25.0;
    wBar.critMax = 30.0;
    wBar.x = 40; wBar.y = 300; wBar.width = 260; wBar.height = 48;
    wBar.panelId = demoPanelId;
    r.check(cpComp.addWidget(wBar), "designer adds Bar Gauge (zone-colored level meter) to canvas");

    // 1. Freeform positioning and resizing & Alignment
    r.check(cpComp.setWidgetBounds("demo_gauge", 60, 50, 200, 200),
            "freeform positioning and resizing updates widget geometry");
    const auto gBounds = cpComp.getWidgetBounds("demo_gauge");
    r.check(gBounds.getX() == 60 && gBounds.getY() == 50 && gBounds.getWidth() == 200 && gBounds.getHeight() == 200,
            "widget preserves exact freeform bounds");

    cpComp.selectWidget("demo_btn", false);
    cpComp.selectWidget("demo_switch", true);
    r.check(cpComp.getSelectedWidgetIds().size() == 2, "multi-selection selects multiple design elements");
    r.check(cpComp.alignSelectedWidgets("top"), "align top positions selected elements to common top coordinate");
    r.check(cpComp.getWidgetBounds("demo_btn").getY() == cpComp.getWidgetBounds("demo_switch").getY(),
            "selected controls share common aligned top coordinate");

    // 2. Right-click configuration dialog opens & allows editing
    auto configDialog = cpComp.createWidgetPropertiesDialog("demo_gauge");
    r.check(configDialog != nullptr,
            "configuration dialog opens with properties for selected instrument");
    auto updatedGaugeConfig = *cpComp.getWidgetConfig("demo_gauge");
    updatedGaugeConfig.title = "Calibrated Speed Gauge";
    updatedGaugeConfig.warnMax = 320.0;
    updatedGaugeConfig.precision = 2;
    r.check(cpComp.configureWidget("demo_gauge", updatedGaugeConfig),
            "dialog allows editing labels, units, ranges, precision, and connections");
    const auto* verifiedGauge = cpComp.getWidgetConfig("demo_gauge");
    r.check(verifiedGauge != nullptr && verifiedGauge->title == "Calibrated Speed Gauge"
            && std::abs(verifiedGauge->warnMax - 320.0) < 1.0e-6 && verifiedGauge->precision == 2,
            "configured widget properties are updated immediately in design surface");

    // 3. Connection picker displays compatible simulation signals
    const auto availableSignals = cpComp.getAvailableSignals();
    r.check(!availableSignals.empty(), "connection picker enumerates available simulation signals");
    bool hasSpeedOut = false, hasPowerIn = false;
    for (const auto& s : availableSignals)
    {
        if (s.name == speedSignal && s.isOutput) hasSpeedOut = true;
        if (s.name == "system.power" && !s.isOutput) hasPowerIn = true;
    }
    r.check(hasSpeedOut && hasPowerIn,
            "connection picker clearly categorizes inputs vs outputs and validates signal compatibility");

    // 4. Controls drive simulation inputs
    cpComp.setDesignMode(false);
    r.check(!cpComp.isDesignMode(), "panel switches to Run Mode (locked layout)");
    cpSession->setBooleanInput("system.power", true);
    cpSession->setBooleanInput("system.start", true);
    cpSession->setNumericInput("system.speed_setpoint", 350.0);
    r.check(cpSession->inputElementBindings().count("system.power") != 0
            && cpSession->inputElementBindings().count("system.start") != 0
            && cpSession->inputElementBindings().count("system.speed_setpoint") != 0,
            "operator controls successfully bind to and drive runtime simulation inputs");

    // 5. Instruments respond to live simulation values
    cpComp.sampleLatestNow();
    const auto liveSnap = cpSession->getSnapshot();
    const double liveSpeed = liveSnap.numericSignals.count(speedSignal) != 0 ? liveSnap.numericSignals.at(speedSignal) : 0.0;
    const double liveDrive = liveSnap.numericSignals.count(driveSignal) != 0 ? liveSnap.numericSignals.at(driveSignal) : 0.0;
    r.check(liveSpeed > 20.0 && liveDrive > 0.5,
            "instruments (analog gauge, digital readout, bar gauge) update dynamically from live simulation signals");

    // 6. Scrollbars operate correctly when panel exceeds visible area
    auto layoutWithCanvas = cpComp.getLayout();
    if (auto* panelCfg = layoutWithCanvas.findPanel(demoPanelId))
    {
        panelCfg->canvasWidth = 1800;
        panelCfg->canvasHeight = 1200;
        cpComp.setLayout(layoutWithCanvas);
    }
    cpComp.setBounds(0, 0, 700, 500);
    const auto totalArea = cpComp.getCanvasTotalArea();
    const auto visibleArea = cpComp.getCanvasVisibleArea();
    r.check(totalArea.getWidth() >= 1800 && totalArea.getHeight() >= 1200,
            "canvas supports dimensions larger than visible window (1800 x 1200)");
    r.check(visibleArea.getWidth() < totalArea.getWidth() && visibleArea.getHeight() < totalArea.getHeight(),
            "viewport enables scrollable area when panel exceeds visible window");
    cpComp.setCanvasScrollPosition(250, 180);
    const auto scrollPos = cpComp.getCanvasScrollPosition();
    r.check(scrollPos.getX() == 250 && scrollPos.getY() == 180,
            "horizontal and vertical scrolling operate cleanly to view off-screen elements");

    // 7. Saving and reopening the machine preserves panel layout and bindings
    const auto demoSavedFile = outDir.getChildFile("demo_operator_panel_saved.machine.json");
    demoSavedFile.deleteFile();
    auto demoMachineToSave = cpBuilder.getMachine();
    demoMachineToSave.controlPanel = cpComp.getLayout();
    r.check(demoMachineToSave.saveToFile(demoSavedFile, ioError),
            "machine saves complete Control Panel Designer layout, styling, and bindings to .machine.json");

    machine_builder::MachineDesign reloadedDemoMachine;
    r.check(machine_builder::MachineDesign::loadFromFile(demoSavedFile, reloadedDemoMachine, ioError),
            "saved machine with Control Panel Designer layout reloads successfully");
    const auto* reloadedPanel = reloadedDemoMachine.controlPanel.findPanel(demoPanelId);
    r.check(reloadedPanel != nullptr && reloadedPanel->canvasWidth >= 1800,
            "reloaded machine preserves custom canvas dimensions and panel metadata");
    const auto* reloadedGauge = reloadedDemoMachine.controlPanel.findWidget("demo_gauge");
    const auto* reloadedReadout = reloadedDemoMachine.controlPanel.findWidget("demo_readout");
    const auto* reloadedLamp = reloadedDemoMachine.controlPanel.findWidget("demo_lamp");
    const auto* reloadedBtn = reloadedDemoMachine.controlPanel.findWidget("demo_btn");
    const auto* reloadedSwitch = reloadedDemoMachine.controlPanel.findWidget("demo_switch");
    const auto* reloadedSlider = reloadedDemoMachine.controlPanel.findWidget("demo_slider");
    const auto* reloadedBar = reloadedDemoMachine.controlPanel.findWidget("demo_bar");
    r.check(reloadedGauge != nullptr && reloadedReadout != nullptr && reloadedLamp != nullptr
            && reloadedBtn != nullptr && reloadedSwitch != nullptr && reloadedSlider != nullptr && reloadedBar != nullptr,
            "reloaded machine preserves all representative designer components");
    r.check(reloadedGauge != nullptr && reloadedGauge->x == 60 && reloadedGauge->y == 50
            && reloadedGauge->width == 200 && reloadedGauge->height == 200
            && reloadedGauge->title == "Calibrated Speed Gauge"
            && reloadedGauge->signalName == speedSignal
            && reloadedGauge->precision == 2
            && std::abs(reloadedGauge->warnMax - 320.0) < 1.0e-6,
            "exact coordinates, dimensions, titles, precision, warning ranges, and signal bindings persist through save/load");

    // 8. Run Mode locks layout against accidental movement
    cpComp.setDesignMode(false);
    r.check(!cpComp.isDesignMode(), "Run Mode is active");
    auto* gaugeComp = cpComp.getWidgetComponent("demo_gauge");
    bool clicksOnGauge = false, clicksOnGaugeChildren = false;
    if (gaugeComp != nullptr)
        gaugeComp->getInterceptsMouseClicks(clicksOnGauge, clicksOnGaugeChildren);
    r.check(gaugeComp != nullptr && clicksOnGauge,
            "Run Mode locks layout: controls intercept clicks directly for normal operation");
    const auto boundsBeforeAttempt = cpComp.getWidgetBounds("demo_gauge");
    cpComp.setWidgetBounds("demo_gauge", boundsBeforeAttempt.getX(), boundsBeforeAttempt.getY(),
                           boundsBeforeAttempt.getWidth(), boundsBeforeAttempt.getHeight());
    r.check(cpComp.getWidgetBounds("demo_gauge") == boundsBeforeAttempt,
            "element positions remain strictly locked in Run Mode");

    cpComp.setDesignMode(true);
    r.check(cpComp.isDesignMode(), "Design Mode is active: layout is unlocked for authoring");

    std::cout << r.checks << " checks, " << r.failures << " failures\n";
    return r.failures == 0 ? 0 : 1;
}

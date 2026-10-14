// Machine Builder UI Tests
// Fast, lightweight, deterministic unit tests for MachineBuilderPanel UI,
// toolbar commands, view navigation, assembly connectivity, and .machine.json persistence.
// ZERO physics simulation, ZERO ODE stepping, completes in < 200 ms.

#include "MachineBuilderPanel.h"

#include <JuceHeader.h>
#include <iostream>
#include <string>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void check(bool condition, const std::string& label)
    {
        ++checks;
        std::printf("%s  %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!condition)
            ++failures;
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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Runner r;
    std::cout << "=== Machine Builder UI Tests ===\n";

    const auto root = juce::File::getCurrentWorkingDirectory();
    const auto example = root.getChildFile("examples").getChildFile("model_driven_motor").getChildFile("nominal");
    const auto outDir = root.getChildFile("Testing").getChildFile("machine_builder_ui");
    outDir.createDirectory();

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel builder(session, nullptr, nullptr);
    builder.setBounds(0, 0, 1200, 760);

    // 1. Toolbar Organization, Tooltips, and Overflow Handling
    const auto groups = builder.getToolbarGroupSummaries();
    r.check(groups.size() == 5
            && groups[0].name == "File"
            && groups[1].name == "Diagram"
            && groups[2].name == "View"
            && groups[3].name == "Simulation"
            && groups[4].name == "Tools",
            "Toolbar groups: File, Diagram, View, Simulation, Tools");

    bool allTooltipsPresent = true;
    size_t totalCommands = 0;
    for (const auto& grp : groups)
    {
        totalCommands += grp.commandIds.size();
        for (const auto& cmd : grp.commandIds)
            if (builder.getToolbarCommandTooltip(cmd).isEmpty())
                allTooltipsPresent = false;
    }
    r.check(totalCommands == 21 && allTooltipsPresent, "All 21 toolbar commands have tooltips");

    r.check(builder.getVisibleToolbarButtonCount() == 21
            && builder.getOverflowToolbarButtonCount() == 0
            && !builder.isOverflowButtonVisible(),
            "All toolbar commands visible without overflow at standard 1200x760 bounds");

    // Narrow window activates overflow button
    builder.setBounds(0, 0, 460, 600);
    r.check(builder.getVisibleToolbarButtonCount() > 0
            && builder.getOverflowToolbarButtonCount() > 0
            && builder.isOverflowButtonVisible(),
            "Narrow window activates toolbar overflow handling");

    // Restore standard width
    builder.setBounds(0, 0, 1200, 760);
    r.check(!builder.isOverflowButtonVisible(), "Restoring width hides overflow button");

    // View navigation commands
    r.check(builder.triggerToolbarCommand("zoom_in")
            && builder.triggerToolbarCommand("zoom_out")
            && builder.triggerToolbarCommand("reset_view")
            && builder.triggerToolbarCommand("fit_all"),
            "Toolbar view commands (zoom_in, zoom_out, reset_view, fit_all) execute cleanly");

    // 2. System Port Management
    std::string err;
    r.check(builder.addSystemPort("power", true, sim_panel::PortDomain::SignalControl, "V", &err),
            "Add system power input port");
    r.check(builder.addSystemPort("start", true, sim_panel::PortDomain::SignalControl, "V", &err),
            "Add system start input port");
    r.check(builder.addSystemPort("stop", true, sim_panel::PortDomain::SignalControl, "V", &err),
            "Add system stop input port");
    r.check(builder.addSystemPort("speed_setpoint", true, sim_panel::PortDomain::SignalControl, "rad/s", &err),
            "Add system speed_setpoint input port");

    // 3. Subsystem Placement & Connection Routing
    if (example.getChildFile("motor_drive.diagram.json").existsAsFile())
    {
        r.check(builder.addSchematicSubsystemFromFile(example.getChildFile("motor_drive.diagram.json"), &err),
                "Add saved schematic subsystem from file");
    }
    if (example.getChildFile("SavedMotorController.frcomp.json").existsAsFile())
    {
        r.check(builder.addFrustSubsystemFromFile(example.getChildFile("SavedMotorController.frcomp.json"), &err),
                "Add saved FRust controller subsystem from file");
    }

    const auto* drive = findByKind(builder.getMachine(), sim_panel::SubsystemKind::SchematicReference);
    const auto* controller = findByKind(builder.getMachine(), sim_panel::SubsystemKind::ProgrammableComponent);
    if (drive != nullptr && controller != nullptr)
    {
        r.check(builder.connectPorts("system", "power", controller->instanceId, "pwr_in", &err),
                "Connect system power port to controller");
        r.check(builder.connectPorts(controller->instanceId, "drive_out", drive->instanceId, "v_in", &err),
                "Connect controller drive port to schematic motor");
    }

    // 4. Persistence Round-Trip (.machine.json)
    const auto savedFile = outDir.getChildFile("ui_test_saved.machine.json");
    savedFile.deleteFile();
    juce::String ioError;
    r.check(builder.saveMachineToFile(savedFile, ioError), "Save machine to .machine.json");

    auto reloadSession = std::make_shared<sim_panel::SystemSimulationSession>();
    machine_builder::MachineBuilderPanel reopened(reloadSession, nullptr, nullptr);
    r.check(reopened.loadMachineFromFile(savedFile, ioError), "Reload machine from .machine.json");
    r.check(reopened.getMachine().assembly.connections.size() == builder.getMachine().assembly.connections.size(),
            "Preserve connection count through save/load");

    // 5. Template Reset
    reopened.loadTemplate("blank");
    r.check(reopened.getMachine().assembly.instances.empty()
            && reopened.getMachine().assembly.connections.empty(),
            "New Blank template resets instances and connections");

    std::cout << "--- Finished with " << r.checks << " checks, " << r.failures << " failures ---\n";
    return r.failures == 0 ? 0 : 1;
}

#pragma once

#include "MachineModel.h"
#include "SystemSimulationSession.h"
#include "MechanicalViewComponent.h"
#include "ControlPanelComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace machine_builder
{

class MachineBuilderPanel final : public juce::Component, private juce::Timer
{
public:
    MachineBuilderPanel();
    MachineBuilderPanel(std::shared_ptr<sim_panel::SystemSimulationSession> session,
                        mechanical_scene::MechanicalViewComponent* mechView,
                        sim_panel::ControlPanelComponent* ctrlPanel);
    ~MachineBuilderPanel() override;

    void setMachine(const MachineDesign& machine);
    const MachineDesign& getMachine() const { return machine_; }
    MachineDesign& getMachine() { return machine_; }

    void loadTemplate(const std::string& templateName);
    bool compileAndRun();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUi();
    void updateDiagnosticsDisplay();

    std::shared_ptr<sim_panel::SystemSimulationSession> session_;
    mechanical_scene::MechanicalViewComponent* mechanicalView_ = nullptr;
    sim_panel::ControlPanelComponent* controlPanel_ = nullptr;

    MachineDesign machine_;

    // UI Controls
    juce::Label titleLabel;
    juce::TextButton newBlankBtn { "New Blank" };
    juce::TextButton loadDualMotorBtn { "Template: Dual-Motor" };
    juce::TextButton loadRobotArmBtn { "Template: Robot Arm" };
    juce::TextButton validateBtn { "Validate" };
    juce::TextButton compileRunBtn { "Compile & Simulate" };

    // Tabs
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::Component> assemblyTab;
    std::unique_ptr<juce::Component> sceneTab;
    std::unique_ptr<juce::Component> diagnosticsTab;

    // Subsystems List / Editor
    juce::ListBox instanceListBox;
    juce::ListBox connectionListBox;
    juce::TextButton addInstanceBtn { "+ Add Subsystem" };
    juce::TextButton removeInstanceBtn { "- Remove" };
    juce::TextButton addConnectionBtn { "+ Connect Ports" };
    juce::TextButton removeConnectionBtn { "- Disconnect" };

    // Diagnostics Viewer
    juce::TextEditor diagnosticsEditor;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineBuilderPanel)
};

} // namespace machine_builder

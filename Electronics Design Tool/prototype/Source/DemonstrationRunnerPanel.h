#pragma once

#include "DemonstrationModel.h"
#include "MachineModel.h"
#include "SystemSimulationSession.h"
#include "MechanicalViewComponent.h"
#include "ControlPanelComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace demo_system
{

class DemonstrationRunnerPanel final : public juce::Component,
                                       public juce::ListBoxModel,
                                       private juce::Timer
{
public:
    DemonstrationRunnerPanel();
    DemonstrationRunnerPanel(std::shared_ptr<sim_panel::SystemSimulationSession> session,
                             mechanical_scene::MechanicalViewComponent* mechView,
                             sim_panel::ControlPanelComponent* ctrlPanel);
    ~DemonstrationRunnerPanel() override;

    void setMachine(const machine_builder::MachineDesign& machine);
    void setDemonstration(const DemonstrationDefinition& def);

    const DemonstrationRunner& getRunner() const { return runner_; }
    DemonstrationRunner& getRunner() { return runner_; }

    void startDemo();
    void pauseDemo();
    void resumeDemo();
    void stopDemo();
    void resetDemo();
    void stepDemo();
    void runFullExperiment();

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUi();
    void updateReportView();

    std::shared_ptr<sim_panel::SystemSimulationSession> session_;
    mechanical_scene::MechanicalViewComponent* mechanicalView_ = nullptr;
    sim_panel::ControlPanelComponent* controlPanel_ = nullptr;

    DemonstrationRunner runner_;

    // Header & Selector controls
    juce::Label titleLabel;
    juce::ComboBox machineSelector;
    juce::ComboBox demoSelector;

    // Execution Buttons
    juce::TextButton startBtn { "Start Demo" };
    juce::TextButton pauseBtn { "Pause" };
    juce::TextButton resumeBtn { "Resume" };
    juce::TextButton stopBtn { "Stop" };
    juce::TextButton resetBtn { "Reset" };
    juce::TextButton stepBtn { "Step" };
    juce::TextButton runExperimentBtn { "Run Full Experiment" };

    // Timeline ListBox
    juce::ListBox actionListBox;

    // Live Observations & Report View
    juce::TextEditor reportViewer;
    juce::Label statusLabel;
};

} // namespace demo_system

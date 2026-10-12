#pragma once

#include "MachineModel.h"
#include "SystemSimulationSession.h"
#include "MechanicalViewComponent.h"
#include "ControlPanelComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
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
    void setCurrentMachineFile(const juce::File& file) { currentMachineFile_ = file; updateDiagnosticsDisplay(); }

    void loadTemplate(const std::string& templateName);
    bool compileAndRun();
    bool startSimulation();
    void stopSimulation();
    void resetSimulation();

    bool addSchematicSubsystemFromFile(const juce::File& file, std::string* error = nullptr);
    bool addFrustSubsystemFromFile(const juce::File& file, std::string* error = nullptr);
    bool addBuiltInMotor(std::string* error = nullptr);
    bool addSystemPort(const std::string& name, bool input, sim_panel::PortDomain domain,
                       const std::string& unit = {}, std::string* error = nullptr);
    bool connectPorts(const std::string& fromInstance, const std::string& fromPort,
                      const std::string& toInstance, const std::string& toPort,
                      std::string* error = nullptr);
    bool saveMachineToFile(const juce::File& file, juce::String& error);
    bool loadMachineFromFile(const juce::File& file, juce::String& error);

    std::function<void(const juce::File&)> openSchematicReference;
    std::function<void(const juce::File&)> openComponentReference;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class AssemblyCanvas;

    void timerCallback() override;
    void refreshUi();
    void updateDiagnosticsDisplay();
    void syncAfterModelEdit();
    void selectInstance(const std::string& instanceId);
    void selectConnection(const std::string& connectionId);
    void applyInspectorEdits();
    void openSelectedReference();
    void removeSelected();
    void chooseSchematicFile();
    void chooseFrustFile();
    void chooseMachineSaveFile();
    void chooseMachineLoadFile();
    void showSystemPortDialog(bool input);
    void resetToBlankMachine();
    void fitMachineView();
    juce::File resolveReference(const std::string& referenceFile) const;
    juce::File referenceBaseFolder() const;
    juce::String selectedInstanceId() const { return juce::String(selectedInstanceId_); }

    std::shared_ptr<sim_panel::SystemSimulationSession> session_;
    mechanical_scene::MechanicalViewComponent* mechanicalView_ = nullptr;
    sim_panel::ControlPanelComponent* controlPanel_ = nullptr;

    MachineDesign machine_;
    juce::File currentMachineFile_;
    std::string selectedInstanceId_;
    std::string selectedConnectionId_;

    // UI Controls
    juce::Label titleLabel;
    juce::TextButton newBlankBtn { "New Blank" };
    juce::TextButton addSchematicBtn { "Add Schematic" };
    juce::TextButton addFrustBtn { "Add FRust" };
    juce::TextButton addMotorBtn { "Add Motor" };
    juce::TextButton addInputBtn { "Add Input" };
    juce::TextButton addOutputBtn { "Add Output" };
    juce::TextButton removeBtn { "Remove" };
    juce::TextButton saveBtn { "Save" };
    juce::TextButton loadBtn { "Load" };
    juce::TextButton fitAllBtn { "Fit All" };
    juce::TextButton validateBtn { "Validate" };
    juce::TextButton compileRunBtn { "Compile" };
    juce::TextButton startBtn { "Start" };
    juce::TextButton stopBtn { "Stop" };
    juce::TextButton resetBtn { "Reset" };
    juce::TextButton applyInspectorBtn { "Apply" };
    juce::TextButton openReferenceBtn { "Open Model" };

    std::unique_ptr<AssemblyCanvas> canvas;
    juce::TextEditor instanceIdEditor;
    juce::TextEditor titleEditor;
    juce::TextEditor referenceEditor;
    juce::TextEditor paramsEditor;
    juce::TextEditor liveSignalsEditor;
    juce::Label inspectorLabel;

    // Diagnostics Viewer
    juce::TextEditor diagnosticsEditor;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MachineBuilderPanel)
};

} // namespace machine_builder

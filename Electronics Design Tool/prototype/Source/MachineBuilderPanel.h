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
    void resetToBlankMachine();
    void fitMachineView();
    void zoomInView();
    void zoomOutView();
    void resetMachineView();

    struct ToolbarGroupSummary
    {
        std::string name;
        std::vector<std::string> commandIds;
        juce::Rectangle<int> headerBounds;
    };

    std::vector<ToolbarGroupSummary> getToolbarGroupSummaries() const;
    int getVisibleToolbarButtonCount() const;
    int getOverflowToolbarButtonCount() const;
    bool isOverflowButtonVisible() const;
    juce::Rectangle<int> getToolbarBounds() const { return toolbarBounds_; }
    juce::Rectangle<int> getCanvasBounds() const;
    bool triggerToolbarCommand(const std::string& commandId);
    juce::String getToolbarCommandTooltip(const std::string& commandId) const;

    bool addSchematicSubsystemFromFile(const juce::File& file, std::string* error = nullptr);
    bool addFrustSubsystemFromFile(const juce::File& file, std::string* error = nullptr);
    bool addBuiltInMotor(std::string* error = nullptr);
    bool addPlotPanel(int channelCount = 4, std::string* error = nullptr);
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

    struct ToolbarItem
    {
        std::string id;
        std::string groupName;
        juce::TextButton* button = nullptr;
        int width = 26;
    };

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
    void showPlotPanelDialog();
    void chooseMachineSaveFile();
    void chooseMachineLoadFile();
    void showSystemPortDialog(bool input);
    void showOverflowMenu();
    void syncPlotPanelBindings();
    void normalizePlotPanelInstance(const std::string& instanceId);
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
    bool sessionCompiled_ = false;

    // UI Controls & Compact Editing Toolbar
    class ToolbarTooltipOverlay;
    std::unique_ptr<ToolbarTooltipOverlay> tooltipOverlay_;
    std::unique_ptr<juce::LookAndFeel> toolbarLookAndFeel_;
    juce::Label titleLabel;
    juce::TextButton newBlankBtn { "New Blank" };
    juce::TextButton addSchematicBtn { "Add Schematic" };
    juce::TextButton addFrustBtn { "Add FRust" };
    juce::TextButton addMotorBtn { "Add Motor" };
    juce::TextButton addPlotBtn { "Add Plot" };
    juce::TextButton addInputBtn { "Add Input" };
    juce::TextButton addOutputBtn { "Add Output" };
    juce::TextButton removeBtn { "Remove" };
    juce::TextButton saveBtn { "Save" };
    juce::TextButton loadBtn { "Load" };
    juce::TextButton fitAllBtn { "Fit All" };
    juce::TextButton zoomInBtn { "Zoom In" };
    juce::TextButton zoomOutBtn { "Zoom Out" };
    juce::TextButton resetViewBtn { "Reset View" };
    juce::TextButton validateBtn { "Validate" };
    juce::TextButton compileRunBtn { "Compile" };
    juce::TextButton startBtn { "Start" };
    juce::TextButton stopBtn { "Stop" };
    juce::TextButton resetBtn { "Reset" };
    juce::TextButton applyInspectorBtn { "Apply" };
    juce::TextButton openReferenceBtn { "Open Model" };
    juce::TextButton overflowBtn { "More" };

    std::vector<ToolbarItem> toolbarItems_;
    std::vector<ToolbarGroupSummary> visibleGroupSummaries_;
    std::vector<int> toolbarSeparators_;
    juce::Rectangle<int> toolbarBounds_;

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

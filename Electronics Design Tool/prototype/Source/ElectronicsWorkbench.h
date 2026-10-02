#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>

class ElectronicsWorkbench final : public juce::Component,
                                   public juce::DragAndDropContainer,
                                   public juce::MenuBarModel
{
public:
    ElectronicsWorkbench();
    ~ElectronicsWorkbench() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuIds
    {
        newProject = 100,
        openProject,
        saveProject,
        resetLayout,
        importComponent,
        runErc,
        runOperatingPoint,
        runTransient,
        runCompiledPreview,
        openAgentSettings,
        exportAgentTools,
        openResearchSpec
    };

    juce::File layoutFile() const;
    juce::File savedProjectFile() const;
    juce::File generatedRunDirectory() const;
    void appendLog(const juce::String& text);
    void resetResearchState();
    void saveProjectFile();
    void openProjectFile();
    void runElectricalRuleCheck();
    void openInstrumentWindow(juce::String refdes, juce::String symbolId);
    void exportAssistantToolManifest();
    juce::String buildAssistantToolManifestJson() const;
    juce::String runElectricalRuleCheckTool();
    juce::String exportCircuitArtifactsTool();
    void showSpecDocument();
    void exportCircuitArtifacts();

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton newButton { "New" };
    juce::TextButton ercButton { "ERC" };
    juce::TextButton transientButton { "Transient" };
    juce::TextButton compileButton { "Compile Preview" };
    juce::ToggleButton stampModeButton { "Stamp" };

    juce::TextEditor* logConsole = nullptr;
    juce::String selectedSymbolId = "resistor";
    juce::OwnedArray<juce::DocumentWindow> floatingInstrumentWindows;
    std::function<void()> resetCircuit;
    std::function<juce::String()> getCircuitJson;
    std::function<juce::String()> getXyceNetlist;
    std::function<juce::String()> getLabInstrumentsJson;
    std::function<juce::String()> getErcReport;
    std::function<bool(const juce::String&, juce::String&)> loadCircuitJson;
    std::function<juce::String(const juce::String&, float, float, const juce::String&,
                               const juce::String&, const juce::String&)> placeSymbolTool;
    std::function<void()> openAgentSettingsDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectronicsWorkbench)
};

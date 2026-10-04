#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>

#include "ProjectStore.h"

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

    // Asks to save an edited diagram, then runs `proceed` unless cancelled.
    void confirmCloseThen(std::function<void()> proceed);
    bool hasUnsavedChanges() const;

private:
    enum MenuIds
    {
        newProject = 100,
        openProject,
        saveProject,           // Save Diagram
        saveDiagramAsItem,
        renameProjectItem,
        newDiagramItem,
        renameDiagramItem,
        duplicateDiagramItem,
        deleteDiagramItem,
        exportSchematicImageItem,
        preferencesItem,
        resetLayout,
        importComponent,
        runErc,
        runOperatingPoint,
        runTransient,
        runCompiledPreview,
        designRlcHighPass,
        autoLayoutDiagramItem,
        toggleStampModeItem,
        openAgentSettings,
        exportAgentTools,
        openResearchSpec
    };

    static constexpr int recentProjectBase = 1000;
    static constexpr int diagramMenuBase = 2000;

    juce::File layoutFile() const;
    juce::File generatedRunDirectory() const;
    void appendLog(const juce::String& text);
    void resetResearchState();

    // Projects and diagrams (ElectronicsWorkbenchProjects.cpp).
    void updateProjectTitle();
    void useProjectMemory();
    void autoSaveCurrentDiagram();
    bool openProjectFolder(const juce::File& folder, juce::String& error);
    bool createNewProject(const juce::File& location, const juce::String& name, juce::String& error);
    bool openDiagram(const juce::String& name, juce::String& error);
    bool createNewDiagram(const juce::String& name, juce::String& error);
    bool saveDiagram(juce::String& error);
    bool saveDiagramAs(const juce::String& name, juce::String& error);
    bool renameDiagram(const juce::String& name, const juce::String& newName, juce::String& error);
    bool deleteDiagram(const juce::String& name, juce::String& error);
    void openMostRecentProject();
    void promptForName(const juce::String& title, const juce::String& message, const juce::String& initial,
                       std::function<void(const juce::String&)> onName);
    void showNewProjectDialog();
    void showOpenProjectDialog();
    void showNewDiagramDialog();
    void handleProjectMenu(int menuItemID);
    void showPreferences();
    std::unique_ptr<juce::DocumentWindow> preferencesWindow;
    int preferenceListener = 0;
    void addProjectMenuItems(juce::PopupMenu& menu);
    juce::String projectInfoJson() const;
    juce::String projectTool(const juce::String& name, const juce::var& args);

    project_store::Project project;
    juce::String currentDiagram;
    juce::String lastSavedJson;
    std::unique_ptr<juce::FileChooser> projectChooser;
    void runElectricalRuleCheck();
    void openInstrumentWindow(juce::String refdes, juce::String symbolId);
    void closeFloatingInstrumentWindows();
    void exportAssistantToolManifest();
    juce::String buildAssistantToolManifestJson() const;
    juce::String cookbookLookupTool(const juce::String& query, int maxCards) const;
    juce::String cookbookCoverageTool() const;
    juce::String cookbookValidateTool() const;
    juce::String cookbookAcceptanceGoalsTool(const juce::String& domainOrId) const;
    juce::String cookbookAcceptanceSummaryTool(const juce::String& domainOrId) const;
    juce::String cookbookAcceptanceStartTool(const juce::String& goalId) const;
    juce::String cookbookAcceptanceRecordTool(const juce::String& reportPath,
                                              const juce::String& evidenceType,
                                              const juce::String& label,
                                              const juce::String& detail,
                                              const juce::String& pathOrValue,
                                              const juce::String& status) const;
    juce::String capabilityGapRecordTool(const juce::String& category,
                                         const juce::String& description,
                                         const juce::String& neededCapability,
                                         const juce::String& evidence,
                                         const juce::String& source,
                                         const juce::String& status) const;
    juce::String runElectricalRuleCheckTool();
    juce::String exportCircuitArtifactsTool();
    juce::String writeAgentMarkdownTool(const juce::String& title, const juce::String& markdown);
    juce::String researchWebSearchTool(const juce::String& query, int maxResults) const;
    juce::String designRlcHighPassFilterTool(double cutoffHz, double impedanceOhms);
    juce::String designPushPullAmplifierTool();
    juce::String autoLayoutDiagramTool();
    void showSpecDocument();
    void exportCircuitArtifacts();
    void designRlcHighPassFilter();
    void autoLayoutDiagram();
    void applySchematicZoom(float zoom);
    void adjustSchematicZoom(float factor);

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton newButton { "New" };
    juce::TextButton ercButton { "ERC" };
    juce::TextButton transientButton { "Transient" };
    juce::TextButton compileButton { "Compile Preview" };
    juce::ToggleButton stampModeButton { "Stamp Mode" };
    juce::ToggleButton snapModeButton { "Snap" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomResetButton { "100%" };
    juce::TextButton zoomInButton { "+" };

    juce::TextEditor* logConsole = nullptr;
    juce::String selectedSymbolId = "resistor";
    juce::OwnedArray<juce::DocumentWindow> floatingInstrumentWindows;
    juce::Component::SafePointer<juce::Component> schematicView;
    std::function<void()> resetCircuit;
    std::function<juce::String()> getCircuitJson;
    std::function<juce::String()> getXyceNetlist;
    std::function<juce::String()> getErcReport;
    std::function<bool(const juce::String&, juce::String&)> loadCircuitJson;
    std::function<juce::String(const juce::String&, float, float, const juce::String&,
                               const juce::String&, const juce::String&)> placeSymbolTool;
    std::function<juce::String(const juce::String&, const juce::String&)> connectNodesTool;
    std::function<juce::String(const juce::String&)> openInstrumentTool;
    std::function<juce::String(double, double)> designHighPassTool;
    std::function<juce::String()> designPushPullTool;
    std::function<juce::String()> autoLayoutTool;
    std::function<juce::String()> exportSchematicImage;
    std::function<void(const juce::File&, const juce::File&, double, double, double, double)> showFrequencyResponse;
    std::function<void()> openAgentSettingsDialog;
    std::function<void(bool)> setSnapEnabled;
    std::function<void(float)> setSchematicZoom;
    std::function<float()> getSchematicZoom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectronicsWorkbench)
};


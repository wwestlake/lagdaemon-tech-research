#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>

#include "ProjectStore.h"
#include "CircuitSolver.h"
#include "FrustEngine.h"

class AudioPipeline;

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
        openDiagramFileItem,
        renameDiagramItem,
        duplicateDiagramItem,
        deleteDiagramItem,
        exportSchematicImageItem,
        preferencesItem,
        resetLayout,
        importComponent,
        runErc,
        runCompiledPreview,
        openAnalyticsItem,
        autoLayoutDiagramItem,
        toggleStampModeItem,
        openAgentSettings,
        exportAgentTools,
        openResearchSpec,
        startAudioSimItem,
        loadAudioSourceItem
    };

    static constexpr int recentProjectBase = 1000;
    static constexpr int diagramMenuBase = 2000;
    static constexpr int analyticsMenuBase = 3000;

    juce::File layoutFile() const;
    juce::File generatedRunDirectory() const;
    void appendLog(const juce::String& text); void appendLog(const juce::String& reason, const juce::String& code, const juce::String& details);
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
    void showOpenDiagramMenu();
    void showOpenDiagramFileDialog();
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
    std::unique_ptr<juce::FileChooser> audioSourceChooser;
    std::unique_ptr<juce::FileChooser> diagramChooser;
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
    juce::String exportFrustRealtimePreviewTool();
    juce::String writeAgentMarkdownTool(const juce::String& title, const juce::String& markdown);
    juce::String researchWebSearchTool(const juce::String& query, int maxResults) const;
    juce::String designRlcHighPassFilterTool(double cutoffHz, double impedanceOhms);
    juce::String designPushPullAmplifierTool();
    juce::String autoLayoutDiagramTool();
    void showSpecDocument();
    void exportCircuitArtifacts();
    void exportFrustRealtimePreview();
    void chooseAudioSourceFile();
    void showAnalytics();
    juce::String analyticsTool(const juce::String& name, const juce::var& args);
    class AnalyticsPanel* analyticsPanel = nullptr;
    class FrustPanel* frustPanel = nullptr;
    class PcbPanel* pcbPanel = nullptr;
    juce::String pcbTool(const juce::String& name, const juce::var& args);
    juce::String pcbLayoutTool(const juce::String& name, const juce::var& args);
    juce::String frustTool(const juce::String& name, const juce::var& args);
    juce::Component::SafePointer<CreationDock::DockPanel> analyticsDockPanel;
    void autoLayoutDiagram();
    void applySchematicZoom(float zoom);
    void adjustSchematicZoom(float factor);

    bool isAudioSimRunning = false;
    std::unique_ptr<AudioPipeline> audioPipeline;
    frust_engine::Engine audioEngine;

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::Label titleLabel;
    juce::TextButton newButton { "New" };
    juce::TextButton openDiagramButton { "Open" };
    juce::TextButton ercButton { "ERC" };
    juce::TextButton transientButton { "Analytics" };
    juce::TextButton compileButton { "Compile Preview" };
    juce::ToggleButton stampModeButton { "Stamp Mode" };
    juce::ToggleButton snapModeButton { "Snap" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomResetButton { "100%" };
    juce::TextButton zoomInButton { "+" };

    juce::TextEditor* logConsole = nullptr;
juce::Component* logPanel = nullptr;
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
    std::function<void()> openAgentSettingsDialog;
    std::function<void(bool)> setSnapEnabled;
    std::function<void(float)> setSchematicZoom;
    std::function<float()> getSchematicZoom;
    std::function<std::tuple<circuit_sim::Circuit, int, int>()> getSimCircuit;
    std::function<std::unordered_map<std::string, double>()> getLiveParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectronicsWorkbench)
};





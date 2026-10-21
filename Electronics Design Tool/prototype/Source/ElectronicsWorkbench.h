#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>

#include "ProjectStore.h"
#include "CircuitSolver.h"
#include "FrustEngine.h"
#include "WorkspaceLayoutStore.h"
#include "PredefinedLayoutCatalog.h"
#include "TestFixturePanel.h"

class AudioPipeline;

struct ComponentPinPlacement;
namespace frust_component { struct Definition; }
namespace sim_panel { class ControlPanelComponent; }
namespace mechanical_scene { class MechanicalViewComponent; }
namespace machine_builder { class MachineBuilderPanel; struct MachineDesign; }
namespace demo_system { class DemonstrationRunnerPanel; struct DemonstrationDefinition; struct DemonstrationRun; }
namespace djehuti_help { class HelpPanel; }
namespace dcad_motion { class DcadMotionClient; }

class DesktopAuthSession;
class ProfileAvatarComponent;
class DjehutiMenuBarComponent;

class ElectronicsWorkbench final : public juce::Component,
                                   public juce::DragAndDropContainer,
                                   public juce::MenuBarModel,
                                   public juce::KeyListener,
                                   private juce::Timer
{
public:
    ElectronicsWorkbench();
    ~ElectronicsWorkbench() override;
#if JUCE_DEBUG
    static int runTestFixtureUiChecks(const juce::File& output, const juce::File& diagram);
#endif

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void openHelp(const juce::String& topicId = {});
    void openHelpUrl(const juce::String& url);
    juce::String helpTool(const juce::String& name, const juce::var& args);

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
        layoutPresetDefault,
        layoutPresetSchematic,
        layoutPresetSimulation,
        layoutPresetMachine,
        layoutPresetProgramming,
        saveLayoutItem,
        manageLayoutsItem,
        importComponent,
        runErc,
        runCompiledPreview,
        openAnalyticsItem,
        openControlPanelItem,
        autoLayoutDiagramItem,
        toggleStampModeItem,
        toggleSnapModeItem,
        zoomInItem,
        zoomOutItem,
        zoomResetItem,
        openAgentSettings,
        exportAgentTools,
        openResearchSpec,
        openDocumentationItem,
        openHelpSchematicItem,
        openHelpControlPanelItem,
        openHelpMachineBuilderItem,
        openHelpFrustItem,
        startAudioSimItem,
        loadAudioSourceItem,
        exportVst3PluginItem,
        cadMotionLinkItem,
        fixtureNewItem,
        fixtureOpenItem,
        fixtureSaveItem,
        fixtureRunItem,
        fixtureAuditionItem,
        fixtureStopItem,
        fixtureExportWavItem,
        fixtureCutToolItem,
        fixtureCloseItem
    };

    static constexpr int recentProjectBase = 1000;
    static constexpr int diagramMenuBase = 2000;
    static constexpr int analyticsMenuBase = 3000;
    static constexpr int viewMenuBase = 4000;
    static constexpr int predefinedLayoutMenuBase = 4500;
    static constexpr int userLayoutMenuBase = 5000;

    std::vector<juce::String> viewMenuIds;
    std::vector<juce::String> predefinedLayoutIds;
    juce::String viewTool(const juce::String& name, const juce::var& args);
    juce::String layoutTool(const juce::String& name, const juce::var& args);

    PredefinedLayoutCatalog predefinedCatalog;
    WorkspaceLayoutStore userLayoutStore;
    void applyPredefinedLayout(const juce::String& presetId);
    void restoreStartupLayout();
    void flushWorkspaceLayout();
    void markLayoutDirty();
    void timerCallback() override;

    bool autoSaveLayoutEnabled = false;
    bool layoutDirty = false;
    uint32_t lastLayoutChangeTime = 0;
    juce::var lastCapturedLayout;
    juce::var lastPersistedLayout;

    void showSaveLayoutDialog();
    void showManageLayoutsDialog();
    void handleSaveLayout(const juce::String& rawName, const juce::var& captured);
    void performSaveLayout(const juce::String& name, const juce::var& captured);
    void restoreUserLayout(const juce::String& name);

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
    juce::String exportVst3PluginTool(const juce::var& args);
    juce::String writeAgentMarkdownTool(const juce::String& title, const juce::String& markdown);
    juce::String researchWebSearchTool(const juce::String& query, int maxResults) const;
    juce::String designRlcHighPassFilterTool(double cutoffHz, double impedanceOhms);
    juce::String designPushPullAmplifierTool();
    juce::String autoLayoutDiagramTool();
    void showSpecDocument();
    void exportCircuitArtifacts();
    void exportFrustRealtimePreview();
    void showExportVst3Dialog();
    void chooseAudioSourceFile();
    void showAnalytics();
    void showControlPanel();
    void showNodeDesigner();
    juce::String analyticsTool(const juce::String& name, const juce::var& args);
    // The tool's settings: its arguments, with param:<id> targets resolved.
    std::map<juce::String, juce::String> analyticsToolSettings(const juce::var& args) const;
    class AnalyticsPanel* analyticsPanel = nullptr;
    sim_panel::ControlPanelComponent* controlPanel = nullptr;
    mechanical_scene::MechanicalViewComponent* mechanicalView = nullptr;
    machine_builder::MachineBuilderPanel* machineBuilder = nullptr;
    demo_system::DemonstrationRunnerPanel* demoRunnerPanel = nullptr;
    class FrustPanel* frustPanel = nullptr;
    class NodeDesignerPanel* nodeDesignerPanel = nullptr;
    class PcbPanel* pcbPanel = nullptr;
    juce::String pcbTool(const juce::String& name, const juce::var& args);
    juce::String pcbLayoutTool(const juce::String& name, const juce::var& args);
    juce::String frustTool(const juce::String& name, const juce::var& args);
    juce::String nodeProgramTool(const juce::String& name, const juce::var& args);
    juce::String controlPanelTool(const juce::String& name, const juce::var& args);
    juce::String assemblyTool(const juce::String& name, const juce::var& args);
    juce::String mechanicalTool(const juce::String& name, const juce::var& args);
    juce::String machineTool(const juce::String& name, const juce::var& args);
    juce::String demoTool(const juce::String& name, const juce::var& args);
    juce::String cadMotionTool(const juce::String& name, const juce::var& args);
    void showMechanicalView();
    void showMachineBuilder();
    void showDemonstrationRunner();
    void showCadMotionLinkDialog();
    std::shared_ptr<dcad_motion::DcadMotionClient> cadMotionClient;
    // FRust execution and the debugger (FrustExecution): frust_run,
    // node_program_run and node_debug_* answer when the program reaches the
    // state asked for, never blocking the message thread.
    bool frustExecutionTool(const juce::String& name, const juce::var& args, std::function<void(juce::String)> done);
    juce::String startNodeDebugging();
    void syncDebugMarkers();
    void executionChanged();
    class FrustDebuggerPanel* debuggerPanel = nullptr;
    // FRust programmable components (FrustComponent) and the common external
    // pin layout (Sub Diagram blocks and components): component_* and
    // pin_layout_* tools, the Properties buttons, the canvas device factory.
    juce::String componentTool(const juce::String& name, const juce::var& args);
    juce::File componentFile(const juce::String& componentName) const;
    // notes: what the program's validation says that does not stop compilation (unused nodes).
    bool compileComponent(const juce::String& componentName, juce::String& error, juce::String* source = nullptr,
                          juce::StringArray* notes = nullptr);
    bool openComponentProgram(const juce::String& componentName, juce::String& error);
    bool applyComponentLayout(const juce::String& componentName, const std::vector<ComponentPinPlacement>& layout, juce::String& error);
    void openPinLayoutEditor(const juce::String& refdes);
    // The library's Programmable Components group, from the project's
    // definitions: refreshed after every change and when the mouse enters
    // the library (files changed elsewhere).
    void refreshComponentPalette();
    std::function<void(const std::vector<std::pair<juce::String, int>>&)> setLibraryComponents;
    juce::String paletteSignature;
    juce::StringArray lastComponentWarnings; // from the last definition edit: wires and probes removed with pins
    // A new definition with no pins (Component1, Component2...) and its empty
    // program; returns its name, or "" with `error`.
    juce::String createBlankComponent(juce::String& error);
    // Library placement: "" or "*new" places a new blank component.
    juce::String placeProgrammableComponent(const juce::String& definition, juce::Point<float> at, juce::String& error);
    // Saves a changed definition (from the Node Designer's Component panel or
    // component_update): renames it if its name changed, keeps pin uids,
    // updates every instance, the open program's context and the library.
    bool storeComponentDefinition(const juce::String& oldName, frust_component::Definition definition,
                                  juce::StringArray& warnings, juce::String& error);
    bool renameComponent(const juce::String& from, const juce::String& to, juce::String& error);
    void saveOpenComponentProgram();
    bool saveOpenComponentProgramStarter(const frust_component::Definition& definition, juce::String& error);
    int executionListener = 0;
    juce::uint64 lastReportedSession = 0;
    juce::Component::SafePointer<CreationDock::DockPanel> analyticsDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> controlPanelDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> mechanicalViewDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> machineBuilderDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> demoRunnerDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> nodeDesignerDockPanel;
    juce::Component::SafePointer<CreationDock::DockPanel> helpDockPanel;
    djehuti_help::HelpPanel* helpPanel = nullptr;
    std::map<juce::String, juce::Component::SafePointer<CreationDock::DockPanel>> openFixtureDockPanels;
    void openTestFixturePanel(const juce::String& fixtureName);
    void createTestFixturePrompt();
    void closeTestFixturePanel(const juce::String& fixtureName);
    TestFixturePanel* findTestFixturePanel(const juce::String& fixtureName) const;
    void autoLayoutDiagram();
    void applySchematicZoom(float zoom);
    void adjustSchematicZoom(float factor);
    void applyWorkspaceLayoutPreset(int presetId);

    bool isAudioSimRunning = false;
    std::unique_ptr<AudioPipeline> audioPipeline;
    frust_engine::Engine audioEngine;

    std::unique_ptr<DjehutiMenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::Label titleLabel;
    bool stampModeEnabled = false;
    std::unique_ptr<DesktopAuthSession> authSession;
    std::unique_ptr<ProfileAvatarComponent> profileControl;

    juce::TextEditor* logConsole = nullptr;
juce::Component* logPanel = nullptr;
    juce::String selectedSymbolId = "resistor";
    juce::OwnedArray<juce::DocumentWindow> floatingInstrumentWindows;
    juce::Component::SafePointer<juce::Component> schematicView;
    std::function<void()> resetCircuit;
    std::function<juce::String()> getCircuitJson;
    std::function<juce::String()> getXyceNetlist;
    std::function<juce::String()> getErcReport;
    std::function<juce::var()> getErcFindings;
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





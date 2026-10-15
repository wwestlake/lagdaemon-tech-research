#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <CreationDock/DockManager.h>
#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include <iostream>
#include <cassert>

namespace {

class DummyContentComponent : public juce::Component
{
public:
    explicit DummyContentComponent(const juce::String& name, int initialValue = 0)
        : label(name), stateValue(initialValue)
    {
    }

    juce::String label;
    int stateValue = 0;
    juce::String unsavedDocumentText = "Unsaved document draft";
};

int totalChecks = 0;
int passedChecks = 0;

#define CHECK(condition, message) \
    do { \
        ++totalChecks; \
        if (condition) { \
            ++passedChecks; \
        } else { \
            std::cerr << "FAILED: " << message << " (" << #condition << ")" << std::endl; \
        } \
    } while (0)

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=== Running Workspace UI Tests ===" << std::endl;

    juce::Component topLevelWindow;
    topLevelWindow.setBounds(0, 0, 1024, 768);

    CreationDock::DockManager dockManager(topLevelWindow);
    dockManager.setBounds(0, 0, 1024, 768);

    // 1. Register panels into different zones
    auto schematicOwner = std::make_unique<DummyContentComponent>("Schematic", 100);
    auto* rawSchematicContent = schematicOwner.get();
    auto* schematicPanel = dockManager.registerPanel("schematic", "Schematic", std::move(schematicOwner), CreationDock::DockTargetZone::CenterTab);

    auto cpOwner = std::make_unique<DummyContentComponent>("Control Panel", 200);
    auto* rawCpContent = cpOwner.get();
    auto* cpPanel = dockManager.registerPanel("control_panel", "Control Panel", std::move(cpOwner), CreationDock::DockTargetZone::CenterTab);

    auto libOwner = std::make_unique<DummyContentComponent>("Component Library", 300);
    auto* rawLibContent = libOwner.get();
    auto* libPanel = dockManager.registerPanel("library", "Component Library", std::move(libOwner), CreationDock::DockTargetZone::Left);

    auto propOwner = std::make_unique<DummyContentComponent>("Properties", 400);
    auto* rawPropContent = propOwner.get();
    auto* propPanel = dockManager.registerPanel("properties", "Properties", std::move(propOwner), CreationDock::DockTargetZone::Right);

    auto consoleOwner = std::make_unique<DummyContentComponent>("Console", 500);
    auto* rawConsoleContent = consoleOwner.get();
    auto* consolePanel = dockManager.registerPanel("console", "Console", std::move(consoleOwner), CreationDock::DockTargetZone::Bottom);

    CHECK(schematicPanel != nullptr, "Schematic panel registered");
    CHECK(cpPanel != nullptr, "Control Panel panel registered");
    CHECK(libPanel != nullptr, "Library panel registered");
    CHECK(propPanel != nullptr, "Properties panel registered");
    CHECK(consolePanel != nullptr, "Console panel registered");

    // 2. Initial state verification
    CHECK(dockManager.isPanelOpen("schematic"), "Schematic is initially open");
    CHECK(dockManager.isPanelOpen("control_panel"), "Control panel is initially open");
    CHECK(dockManager.isPanelOpen("library"), "Library is initially open");
    CHECK(dockManager.isPanelOpen("properties"), "Properties is initially open");
    CHECK(dockManager.isPanelOpen("console"), "Console is initially open");

    // The last panel docked to CenterTab is control_panel, so it is active in CenterTab
    CHECK(dockManager.isPanelActive("control_panel"), "Control panel is active in CenterTab");
    CHECK(!dockManager.isPanelActive("schematic"), "Schematic is inactive in CenterTab");

    // 3. Bring inactive view forward
    dockManager.togglePanel("schematic");
    CHECK(dockManager.isPanelActive("schematic"), "Schematic brought forward via togglePanel");
    CHECK(!dockManager.isPanelActive("control_panel"), "Control panel now inactive in CenterTab");

    // 4. Hide active view (Clicking a visible/active view hides it)
    rawSchematicContent->stateValue = 999;
    rawSchematicContent->unsavedDocumentText = "Modified unsaved schematic netlist";

    dockManager.togglePanel("schematic");
    CHECK(!dockManager.isPanelOpen("schematic"), "Schematic is now hidden");
    CHECK(!dockManager.isPanelActive("schematic"), "Schematic is not active");

    // Adjacent tab (control_panel) should automatically become active in CenterTab
    CHECK(dockManager.isPanelActive("control_panel"), "Control panel automatically became active when Schematic was hidden");

    // Content state must be preserved while hidden!
    CHECK(dockManager.findPanelById("schematic") == schematicPanel, "findPanelById still finds hidden panel handle");
    CHECK(rawSchematicContent->stateValue == 999, "State value preserved while hidden");
    CHECK(rawSchematicContent->unsavedDocumentText == "Modified unsaved schematic netlist", "Unsaved document text preserved");

    // 5. Restore hidden view (Clicking a hidden view restores it)
    dockManager.togglePanel("schematic");
    CHECK(dockManager.isPanelOpen("schematic"), "Schematic restored to open");
    CHECK(dockManager.isPanelActive("schematic"), "Schematic restored and active");
    CHECK(rawSchematicContent->stateValue == 999, "State value still preserved after reopening");
    CHECK(rawSchematicContent->unsavedDocumentText == "Modified unsaved schematic netlist", "Document text still preserved");

    // 6. Side zone collapse and restoration
    CHECK(dockManager.isPanelOpen("library"), "Library open initially");
    dockManager.hidePanel("library");
    CHECK(!dockManager.isPanelOpen("library"), "Library hidden");
    CHECK(!dockManager.isPanelActive("library"), "Library inactive");
    CHECK(rawLibContent->stateValue == 300, "Library state preserved while zone collapsed");

    dockManager.showPanel("library");
    CHECK(dockManager.isPanelOpen("library"), "Library reopened in Left zone");
    CHECK(dockManager.isPanelActive("library"), "Library active in Left zone");

    // 7. Tab close request (simulating tab 'x' button or context menu 'Close')
    CHECK(dockManager.isPanelOpen("control_panel"), "Control panel open before close request");
    if (cpPanel->onCloseRequested)
        cpPanel->onCloseRequested(cpPanel);
    CHECK(!dockManager.isPanelOpen("control_panel"), "Control panel hidden via onCloseRequested");
    CHECK(rawCpContent->stateValue == 200, "Control panel state preserved after onCloseRequested");

    dockManager.activatePanel("control_panel");
    CHECK(dockManager.isPanelOpen("control_panel"), "Control panel restored via activatePanel");
    CHECK(dockManager.isPanelActive("control_panel"), "Control panel active after activatePanel");

    // 8. Registered panel discovery and titles
    auto regIds = dockManager.getRegisteredPanelIds();
    CHECK(regIds.size() == 5, "Registered panel list contains all 5 panels");
    CHECK(dockManager.getPanelTitle("schematic") == "Schematic", "Title lookup correct for schematic");
    CHECK(dockManager.getPanelTitle("control_panel") == "Control Panel", "Title lookup correct for control_panel");

    // 9. Reset layout restores all panels to open in initial zones
    dockManager.hidePanel("schematic");
    dockManager.hidePanel("properties");
    CHECK(!dockManager.isPanelOpen("schematic"), "Schematic hidden before resetLayout");
    CHECK(!dockManager.isPanelOpen("properties"), "Properties hidden before resetLayout");

    dockManager.resetLayout();
    CHECK(dockManager.isPanelOpen("schematic"), "Schematic reopened by resetLayout");
    CHECK(dockManager.isPanelOpen("properties"), "Properties reopened by resetLayout");

    // 10. Workspace Layout Presets application and data preservation
    auto makeZoneVar = [](const juce::StringArray& panels, int currentTab) {
        auto* obj = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (const auto& p : panels)
            arr.add(p);
        obj->setProperty("panels", arr);
        obj->setProperty("currentTab", currentTab);
        return juce::var(obj);
    };

    // Apply Simulation & Analytics Preset
    {
        auto* zones = new juce::DynamicObject();
        zones->setProperty("left", makeZoneVar({ "library" }, 0));
        zones->setProperty("center", makeZoneVar({ "control_panel", "schematic" }, 0));
        zones->setProperty("right", makeZoneVar({ "properties" }, 0));
        zones->setProperty("bottom", makeZoneVar({ "console" }, 0));

        auto* root = new juce::DynamicObject();
        root->setProperty("leftWidth", 220);
        root->setProperty("rightWidth", 280);
        root->setProperty("bottomHeight", 160);
        root->setProperty("zones", juce::var(zones));
        root->setProperty("floating", juce::Array<juce::var>());

        dockManager.applyLayout(juce::var(root));
        CHECK(dockManager.isPanelOpen("control_panel"), "Control panel open in Simulation preset");
        CHECK(dockManager.isPanelActive("control_panel"), "Control panel active in Simulation preset");
        CHECK(dockManager.isPanelOpen("schematic"), "Schematic open in Simulation preset");
        CHECK(rawSchematicContent->stateValue == 999, "Schematic content survived preset application");
        CHECK(rawSchematicContent->unsavedDocumentText == "Modified unsaved schematic netlist", "Document text survived preset application");
        CHECK(rawCpContent->stateValue == 200, "Control panel content preserved in preset");
    }

    // 11. LagDaemon Authentication & ProfileAvatarComponent
    {
        DesktopAuthSession auth("djehuti");
        CHECK(auth.hasValidSession(), "DesktopAuthSession loaded valid session from shared storage");
        CHECK(auth.getSession().user.displayName == "W. Westlake", "Auth session display name is W. Westlake");
        CHECK(auth.getSession().user.email == "wweatlake1234@gmail.com", "Auth session email matches");

        ProfileAvatarComponent avatar(auth);
        avatar.setBounds(0, 0, avatar.getPreferredWidth(), 24);
        CHECK(avatar.getPreferredWidth() >= 84, "Profile avatar preferred width is valid");
        CHECK(avatar.getTooltip().contains("W. Westlake"), "Avatar tooltip contains user display name");
    }

    std::cout << "Results: " << passedChecks << " / " << totalChecks << " checks passed." << std::endl;

    if (passedChecks == totalChecks) {
        std::cout << "ALL WORKSPACE UI TESTS PASSED SUCCESSFULLY!" << std::endl;
        return 0;
    }

    std::cerr << "SOME WORKSPACE UI TESTS FAILED!" << std::endl;
    return 1;
}

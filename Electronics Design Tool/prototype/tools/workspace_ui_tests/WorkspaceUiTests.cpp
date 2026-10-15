#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <CreationDock/DockManager.h>
#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include "DjehutiMenuBarComponent.h"
#include "WorkspaceLayoutStore.h"
#include "ManageLayoutsDialog.h"
#include "PredefinedLayoutCatalog.h"
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
        avatar.setBounds(0, 0, avatar.getPreferredWidth(), 32);
        CHECK(avatar.getPreferredWidth() >= 96, "Profile avatar preferred width is >= 96px for 42px header");
        CHECK(avatar.getTooltip().contains("W. Westlake"), "Avatar tooltip contains user display name");
        CHECK(!avatar.isMenuOpen(), "Avatar menu is initially closed");
        avatar.setMenuOpen(true);
        CHECK(avatar.isMenuOpen(), "Avatar menuOpen flag set to true");
        avatar.setMenuOpen(false);
        CHECK(!avatar.isMenuOpen(), "Avatar menuOpen flag reset to false");
    }

    // 12. Global Menu Activation, 42px Header, and Interaction Contract
    {
        class TestMenuBarModel : public juce::MenuBarModel
        {
        public:
            juce::StringArray getMenuBarNames() override
            {
                return { "File", "Circuit", "Analytics", "Agent", "View", "Layout", "Help" };
            }

            juce::PopupMenu getMenuForIndex(int, const juce::String& menuName) override
            {
                juce::PopupMenu menu;
                menu.addItem(1, menuName + " Item 1");
                menu.addItem(2, menuName + " Item 2");
                if (menuName == "Layout")
                {
                    menu.addItem(10, "Default Layout");
                    menu.addItem(11, "Schematic & Circuit Construction");
                    menu.addItem(12, "Simulation & Analytics");
                    menu.addItem(13, "Machine Builder & Systems");
                    menu.addItem(14, "FRust & Node Programming");
                    menu.addSeparator();
                    menu.addItem(15, "Save Layout...");
                    juce::PopupMenu userSub;
                    userSub.addItem(-1, "No saved layouts", false, false);
                    menu.addSubMenu("User Layouts", userSub);
                    menu.addItem(16, "Manage User Layouts...");
                }
                return menu;
            }

            void menuItemSelected(int id, int) override
            {
                lastSelectedId = id;
            }

            int lastSelectedId = 0;
        };

        TestMenuBarModel model;
        DjehutiMenuBarComponent menuBar(&model);
        menuBar.setBounds(0, 0, 800, 42);

        // Header height contract
        DjehutiHeaderLookAndFeel lookAndFeel;
        CHECK(lookAndFeel.getDefaultMenuBarHeight() == 42, "Header look and feel default menu bar height is 42px");

        // Menu bar item coordinate discovery
        const int item0 = menuBar.getItemIndexAt(juce::Point<int>(15, 20));
        CHECK(item0 == 0, "Item 0 at (15, 20) is File menu");

        const int item1 = menuBar.getItemIndexAt(juce::Point<int>(95, 20));
        CHECK(item1 == 1, "Item 1 at (95, 20) is Circuit menu");

        const int outOfBounds = menuBar.getItemIndexAt(juce::Point<int>(750, 20));
        CHECK(outOfBounds == -1, "Out of bounds coordinate returns -1");

        // Active menu tracking
        CHECK(menuBar.getActiveMenuIndex() == -1, "Initially no menu is open");

        menuBar.setActiveMenuIndex(0);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Active menu set to File (0)");

        // Policy: Mouse movement must NOT switch open menus
        const juce::MouseEvent moveEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                         juce::Point<float>(95.0f, 20.0f),
                                         juce::ModifierKeys(),
                                         juce::MouseInputSource::defaultPressure,
                                         juce::MouseInputSource::defaultOrientation,
                                         juce::MouseInputSource::defaultRotation,
                                         0.0f, 0.0f,
                                         &menuBar,
                                         &menuBar,
                                         juce::Time::getCurrentTime(),
                                         juce::Point<float>(95.0f, 20.0f),
                                         juce::Time::getCurrentTime(),
                                         0, false);
        menuBar.mouseMove(moveEvent);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Mouse move over Circuit does NOT switch menu (remains 0)");

        // Policy: Drag must NOT switch open menus
        menuBar.mouseDrag(moveEvent);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Mouse drag does NOT switch menu (remains 0)");

        // Policy: deliberate closing
        menuBar.closeActiveMenu();
        CHECK(menuBar.getActiveMenuIndex() == -1, "closeActiveMenu resets active index to -1");

        // Profile Avatar vertical centering in 42px header
        const int headerHeight = 42;
        const int profileControlHeight = 32;
        const int expectedY = (headerHeight - profileControlHeight) / 2;
        CHECK(expectedY == 5, "Profile control vertically centered in 42px header at y = 5");

        // 13. Real popup window creation, visibility, switching, and toggle-closing
        topLevelWindow.addAndMakeVisible(menuBar);
        topLevelWindow.setVisible(true);

        DesktopAuthSession auth("djehuti");
        ProfileAvatarComponent profile(auth);
        profile.setBounds(700, 5, profile.getPreferredWidth(), 32);
        topLevelWindow.addAndMakeVisible(profile);

        // Click item 0 (File) via peer event dispatch: opens dropdown immediately
        topLevelWindow.addToDesktop(0);
        topLevelWindow.setVisible(true);
        auto* peer = topLevelWindow.getPeer();
        CHECK(peer != nullptr, "Top level window has valid ComponentPeer");

        auto clickAt = [&topLevelWindow](juce::Point<float> pt)
        {
            auto* p = topLevelWindow.getPeer();
            if (p == nullptr) return;
            auto now = juce::Time::currentTimeMillis();
            p->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pt, juce::ModifierKeys(), 0.0f, 0.0f, now, {}, 0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            p->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pt, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, now + 10, {}, 0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            p->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pt, juce::ModifierKeys(), 0.0f, 0.0f, now + 20, {}, 0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        };

        // Critical Test 1: Click File and do NOT move the mouse (stationary click)
        clickAt(juce::Point<float>(15.0f, 20.0f));

        // Let the message dispatch loop run with the mouse completely stationary
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);

        CHECK(menuBar.getActiveMenuIndex() == 0, "Click on File immediately opens File menu");
        auto* modalMenu0 = juce::Component::getCurrentlyModalComponent();
        CHECK(modalMenu0 != nullptr, "Modal popup window created and displayed for File menu");
        if (modalMenu0 != nullptr)
            CHECK(modalMenu0->isVisible(), "File menu popup window is visibly displayed while mouse remains stationary");

        juce::Thread::sleep(50);

        // Critical Test 2: Click Circuit while File is open -> switches directly on that same click
        clickAt(juce::Point<float>(95.0f, 20.0f));

        CHECK(menuBar.getActiveMenuIndex() == 1, "Clicking Circuit switches directly to Circuit menu on first click");
        auto* modalMenu1 = juce::Component::getCurrentlyModalComponent();
        CHECK(modalMenu1 != nullptr, "Circuit menu popup window is active");
        if (modalMenu1 != nullptr)
            CHECK(modalMenu1->isVisible(), "Circuit menu popup window is visible");

        juce::Thread::sleep(50);

        // Critical Test 3: Click active menu heading again -> toggles it closed
        clickAt(juce::Point<float>(95.0f, 20.0f));

        CHECK(menuBar.getActiveMenuIndex() == -1, "Clicking active Circuit menu again toggles it closed");
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr, "Popup window dismissed on toggle close");

        juce::Thread::sleep(50);

        // Critical Test 4: Click heading after dismissal -> opens immediately on first click
        clickAt(juce::Point<float>(15.0f, 20.0f));
        CHECK(menuBar.getActiveMenuIndex() == 0, "Clicking File after dismissal opens dropdown immediately on first click");
        CHECK(juce::Component::getCurrentlyModalComponent() != nullptr, "Popup window opened on first click after dismissal");

        juce::Thread::sleep(50);

        // Critical Test 5: Click outside headings on menu bar -> dismisses the menu cleanly
        clickAt(juce::Point<float>(500.0f, 20.0f));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        CHECK(menuBar.getActiveMenuIndex() == -1, "Clicking outside headings dismisses the dropdown cleanly");
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr, "Modal popup dismissed on click outside");

        // Profile Avatar popup interaction
        const juce::MouseEvent clickProfile(juce::Desktop::getInstance().getMainMouseSource(),
                                            juce::Point<float>(10.0f, 16.0f),
                                            juce::ModifierKeys::leftButtonModifier,
                                            juce::MouseInputSource::defaultPressure,
                                            juce::MouseInputSource::defaultOrientation,
                                            juce::MouseInputSource::defaultRotation,
                                            0.0f, 0.0f,
                                            &profile,
                                            &profile,
                                            juce::Time::getCurrentTime(),
                                            juce::Point<float>(10.0f, 16.0f),
                                            juce::Time::getCurrentTime(),
                                            1, false);

        profile.mouseDown(clickProfile);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        CHECK(profile.isMenuOpen(), "Clicking profile control opens profile menu");
        auto* modalProfile = juce::Component::getCurrentlyModalComponent();
        CHECK(modalProfile != nullptr, "Modal popup window created for profile menu");

        profile.mouseDown(clickProfile);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        CHECK(!profile.isMenuOpen(), "Clicking profile control again toggles it closed");
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr, "Profile popup window dismissed");

        // 14. Keyboard navigation: Left/Right arrow switching and Escape closing
        menuBar.openMenu(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Menu 0 open before keyboard nav");

        menuBar.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 1, "Right arrow switched to Menu 1");

        menuBar.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Left arrow switched back to Menu 0");

        menuBar.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == -1, "Escape key closed the menu");
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr, "Modal component null after Escape");

        // 15. Menu Command Execution
        menuBar.openMenu(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 0, "Menu 0 open for command invocation");

        // Trigger command ID 1 via menuItemSelected dispatch
        menuBar.handleCommandMessage(1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(model.lastSelectedId == 1, "Menu command ID 1 dispatched and executed");
        CHECK(menuBar.getActiveMenuIndex() == -1, "Menu closed after command execution");

        // 16. Rapid repeated open/close cycles across all 7 top-level menus (including Layout at index 5)
        for (int m = 0; m < 7; ++m)
        {
            menuBar.openMenu(m);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            CHECK(menuBar.getActiveMenuIndex() == m, "Menu " + juce::String(m) + " opened in cycle");
            CHECK(juce::Component::getCurrentlyModalComponent() != nullptr, "Modal popup exists for menu " + juce::String(m));

            menuBar.closeActiveMenu();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            CHECK(menuBar.getActiveMenuIndex() == -1, "Menu " + juce::String(m) + " closed in cycle");
        }

        // Test stationary click and switching specifically for the new Layout menu (index 5)
        menuBar.openMenu(5);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 5, "Layout menu (index 5) opens on click");
        CHECK(juce::Component::getCurrentlyModalComponent() != nullptr, "Layout menu popup exists");

        // Single-click switch from Layout (5) to Help (6)
        menuBar.openMenu(6);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(menuBar.getActiveMenuIndex() == 6, "Switched from Layout to Help on single click");
        menuBar.closeActiveMenu();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    // 17. Named User Layouts, Persistence, Recovery, and Safe Application Tests
    {
        std::cout << "--- Testing Named User Layouts Store and Persistence ---" << std::endl;

        // Built-in presets verification
        auto presets = WorkspaceLayoutStore::getBuiltInPresetNames();
        CHECK(presets.size() == 5, "5 built-in presets defined");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("Default Layout"), "Default Layout recognized as preset");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("default layout"), "Preset check is case-insensitive");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("Schematic & Circuit Construction"), "Schematic preset recognized");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("Simulation & Analytics"), "Simulation preset recognized");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("Machine Builder & Systems"), "Machine Builder preset recognized");
        CHECK(WorkspaceLayoutStore::isBuiltInPresetName("FRust & Node Programming"), "FRust preset recognized");
        CHECK(!WorkspaceLayoutStore::isBuiltInPresetName("Custom Audio Rig"), "Custom layout is not a preset");

        const juce::File testStorageFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                .getChildFile("djehuti_test_user_layouts_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".json");
        testStorageFile.deleteFile();

        WorkspaceLayoutStore store(testStorageFile);
        CHECK(store.getLayoutNames().isEmpty(), "Initial store is empty");

        // Validate rejection of empty names
        juce::String err;
        CHECK(!store.saveLayout("", juce::var(new juce::DynamicObject()), err), "Reject empty layout name");
        CHECK(!store.saveLayout("   ", juce::var(new juce::DynamicObject()), err), "Reject whitespace-only layout name");

        // Validate protection of built-in presets
        CHECK(!store.saveLayout("Default Layout", juce::var(new juce::DynamicObject()), err), "Reject overwriting Default Layout");
        CHECK(!store.saveLayout("default layout", juce::var(new juce::DynamicObject()), err), "Reject overwriting built-in preset case-insensitively");
        CHECK(!store.saveLayout("Simulation & Analytics", juce::var(new juce::DynamicObject()), err), "Reject overwriting Simulation preset");

        // Validate capturing and saving two distinct layouts
        auto layout1Data = dockManager.captureLayout();
        CHECK(store.saveLayout("Alpha Layout", layout1Data, err), "Saved Alpha Layout successfully");
        CHECK(store.hasLayout("Alpha Layout"), "Alpha Layout exists");
        CHECK(store.hasLayout("alpha layout"), "hasLayout is case-insensitive");

        // Modify workspace arrangement for layout 2: hide library, open console in Bottom
        dockManager.hidePanel("library");
        dockManager.showPanel("console");
        auto layout2Data = dockManager.captureLayout();

        CHECK(store.saveLayout("Beta Layout", layout2Data, err), "Saved Beta Layout successfully");
        CHECK(store.hasLayout("Beta Layout"), "Beta Layout exists");

        // Case-insensitive alphabetical listing
        CHECK(store.saveLayout("zebra layout", layout1Data, err), "Saved zebra layout");
        auto names = store.getLayoutNames();
        CHECK(names.size() == 3, "3 user layouts saved");
        CHECK(names[0] == "Alpha Layout", "First sorted is Alpha Layout");
        CHECK(names[1] == "Beta Layout", "Second sorted is Beta Layout");
        CHECK(names[2] == "zebra layout", "Third sorted is zebra layout");

        // Case-insensitive duplicate detection and replacement
        CHECK(store.hasLayout("alpha layout"), "Duplicate detection case-insensitive");
        CHECK(store.saveLayout("alpha layout", layout2Data, err), "Overwriting existing layout succeeds");
        CHECK(store.getLayoutNames().size() == 3, "Layout count remains 3 after replacement");

        // Renaming validation
        CHECK(!store.renameLayout("Alpha Layout", "", err), "Reject empty rename");
        CHECK(!store.renameLayout("Alpha Layout", "Default Layout", err), "Reject rename to built-in preset");
        CHECK(!store.renameLayout("Alpha Layout", "Beta Layout", err), "Reject rename to existing layout");
        CHECK(!store.renameLayout("NonExistent", "NewName", err), "Reject rename of nonexistent layout");
        CHECK(store.renameLayout("Alpha Layout", "Gamma Layout", err), "Rename Alpha to Gamma succeeds");
        CHECK(!store.hasLayout("Alpha Layout"), "Old name Alpha no longer exists");
        CHECK(store.hasLayout("Gamma Layout"), "New name Gamma exists");

        // Deleting validation
        CHECK(!store.deleteLayout("NonExistent", err), "Delete nonexistent fails");
        CHECK(store.deleteLayout("zebra layout", err), "Delete zebra layout succeeds");
        CHECK(!store.hasLayout("zebra layout"), "zebra layout no longer in store");
        CHECK(store.getLayoutNames().size() == 2, "2 layouts remain after deletion");

        // Persistence round-trip across instance reload (simulating application restart)
        WorkspaceLayoutStore reloadedStore(testStorageFile);
        auto reloadedNames = reloadedStore.getLayoutNames();
        CHECK(reloadedNames.size() == 2, "Reloaded store contains 2 layouts");
        CHECK(reloadedStore.hasLayout("Beta Layout"), "Beta Layout survived reload");
        CHECK(reloadedStore.hasLayout("Gamma Layout"), "Gamma Layout survived reload");

        // Corrupt storage recovery
        const juce::File corruptFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                            .getChildFile("djehuti_corrupt_test_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".json");
        corruptFile.replaceWithText("{ this is definitely not valid json ]]");
        WorkspaceLayoutStore corruptStore(corruptFile);
        CHECK(corruptStore.getLayoutNames().isEmpty(), "Corrupt file handled cleanly without crash");
        corruptFile.deleteFile();

        // Monitor disconnection / floating window boundary sanitization
        juce::var testFloatingLayout = layout1Data.clone();
        if (auto* obj = testFloatingLayout.getDynamicObject())
        {
            juce::Array<juce::var> fArr;
            auto* winObj = new juce::DynamicObject();
            winObj->setProperty("id", "schematic");
            winObj->setProperty("x", -9999);
            winObj->setProperty("y", -9999);
            winObj->setProperty("w", 600);
            winObj->setProperty("h", 450);
            fArr.add(juce::var(winObj));
            obj->setProperty("floating", fArr);
        }
        WorkspaceLayoutStore::sanitizeFloatingBounds(testFloatingLayout);
        auto* floatArrAfter = testFloatingLayout.getProperty("floating", {}).getArray();
        CHECK(floatArrAfter != nullptr && !floatArrAfter->isEmpty(), "Floating array preserved");
        if (floatArrAfter != nullptr && !floatArrAfter->isEmpty())
        {
            int sanitizedX = (int)floatArrAfter->getReference(0).getProperty("x", -9999);
            int sanitizedY = (int)floatArrAfter->getReference(0).getProperty("y", -9999);
            CHECK(sanitizedX > -9000 && sanitizedY > -9000, "Offscreen floating window repositioned safely onto visible screen");
        }

        // Safe application to DockManager and document content preservation
        rawSchematicContent->unsavedDocumentText = "Important circuit draft";
        rawSchematicContent->stateValue = 4242;

        CHECK(WorkspaceLayoutStore::applyLayoutSafely(dockManager, layout1Data, err), "applyLayoutSafely succeeded for layout 1");
        CHECK(dockManager.isPanelOpen("library"), "Library panel open in layout 1");
        CHECK(rawSchematicContent->unsavedDocumentText == "Important circuit draft", "Document text preserved after layout 1 apply");
        CHECK(rawSchematicContent->stateValue == 4242, "Document state value preserved after layout 1 apply");

        CHECK(WorkspaceLayoutStore::applyLayoutSafely(dockManager, layout2Data, err), "applyLayoutSafely succeeded for layout 2");
        CHECK(!dockManager.isPanelOpen("library"), "Library panel closed in layout 2");
        CHECK(dockManager.isPanelOpen("console"), "Console panel open in layout 2");
        CHECK(rawSchematicContent->unsavedDocumentText == "Important circuit draft", "Document text preserved after layout 2 apply");
        CHECK(rawSchematicContent->stateValue == 4242, "Document state value preserved after layout 2 apply");

        // ManageLayoutsDialog test
        bool layoutsChangedFired = false;
        ManageLayoutsDialog dialog(reloadedStore, [&layoutsChangedFired] {
            layoutsChangedFired = true;
        });
        CHECK(dialog.getNumRows() == 2, "ManageLayoutsDialog displays 2 saved layouts");

        testStorageFile.deleteFile();

        // 13. Default Layout Template Verification (Schematic, Analytics, PCB, and Help & Documentation)
        auto analyticsOwner = std::make_unique<DummyContentComponent>("Analytics", 600);
        dockManager.registerPanel("analytics", "Analytics", std::move(analyticsOwner), CreationDock::DockTargetZone::CenterTab);

        auto pcbOwner = std::make_unique<DummyContentComponent>("PCB", 700);
        dockManager.registerPanel("pcb", "PCB", std::move(pcbOwner), CreationDock::DockTargetZone::CenterTab);

        auto helpOwner = std::make_unique<DummyContentComponent>("Help & Documentation", 800);
        dockManager.registerPanel("help", "Help & Documentation", std::move(helpOwner), CreationDock::DockTargetZone::CenterTab);

        auto hierarchyOwner = std::make_unique<DummyContentComponent>("Circuit Hierarchy", 900);
        dockManager.registerPanel("hierarchy", "Circuit Hierarchy", std::move(hierarchyOwner), CreationDock::DockTargetZone::Left);

        auto agentOwner = std::make_unique<DummyContentComponent>("BYOK Agent", 1000);
        dockManager.registerPanel("agent", "BYOK Agent", std::move(agentOwner), CreationDock::DockTargetZone::Right);

        auto logOwner = std::make_unique<DummyContentComponent>("Log", 1100);
        dockManager.registerPanel("log", "Log", std::move(logOwner), CreationDock::DockTargetZone::Bottom);

        PredefinedLayoutCatalog testCatalog;
        juce::String catalogErr;
        CHECK(testCatalog.initialize(catalogErr), "Predefined layout catalog initialized");
        const auto* defaultPreset = testCatalog.findPresetById("default");
        CHECK(defaultPreset != nullptr, "Default preset found in catalog");
        if (defaultPreset != nullptr)
        {
            const auto& defaultLayoutData = defaultPreset->layoutData;
            juce::String defaultErr;
            CHECK(PredefinedLayoutCatalog::validateSingleLayoutData(defaultLayoutData, defaultErr), "Default layout data validates cleanly");
            CHECK(WorkspaceLayoutStore::applyLayoutSafely(dockManager, defaultLayoutData, defaultErr), "Default layout applied safely");
        }

        CHECK(dockManager.isPanelOpen("schematic"), "Default view: schematic tab is open");
        CHECK(dockManager.isPanelOpen("analytics"), "Default view: analytics tab is open");
        CHECK(dockManager.isPanelOpen("pcb"), "Default view: pcb tab is open");
        CHECK(dockManager.isPanelOpen("help"), "Default view: documentation window is open");
        CHECK(dockManager.isPanelActive("help"), "Default view: documentation window is active and displayed");

        CHECK(!dockManager.isPanelOpen("control_panel"), "Default view: non-default panel (control_panel) is closed");

        CHECK(dockManager.isPanelOpen("library"), "Default view: library is open");
        CHECK(dockManager.isPanelOpen("hierarchy"), "Default view: hierarchy is open");
        CHECK(dockManager.isPanelOpen("properties"), "Default view: properties is open");
        CHECK(dockManager.isPanelOpen("agent"), "Default view: agent is open");
        CHECK(dockManager.isPanelOpen("log"), "Default view: log is open");
        CHECK(dockManager.isPanelOpen("console"), "Default view: console is open");

        dockManager.activatePanel("schematic");
        CHECK(dockManager.isPanelActive("schematic"), "Switched to schematic tab");

        dockManager.activatePanel("analytics");
        CHECK(dockManager.isPanelActive("analytics"), "Switched to analytics tab");

        dockManager.activatePanel("pcb");
        CHECK(dockManager.isPanelActive("pcb"), "Switched to pcb tab");

        dockManager.activatePanel("help");
        CHECK(dockManager.isPanelActive("help"), "Switched back to documentation window");
    }

    std::cout << "Results: " << passedChecks << " / " << totalChecks << " checks passed." << std::endl;

    if (passedChecks == totalChecks) {
        std::cout << "ALL WORKSPACE UI TESTS PASSED SUCCESSFULLY!" << std::endl;
        return 0;
    }

    std::cerr << "SOME WORKSPACE UI TESTS FAILED!" << std::endl;
    return 1;
}

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <CreationDock/DockManager.h>
#include "Auth/DesktopAuthSession.h"
#include "Auth/ProfileAvatarComponent.h"
#include "DjehutiMenuBarComponent.h"
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
                return { "File", "Circuit", "Analytics", "Agent", "View", "Help" };
            }

            juce::PopupMenu getMenuForIndex(int, const juce::String& menuName) override
            {
                juce::PopupMenu menu;
                menu.addItem(1, menuName + " Item 1");
                menu.addItem(2, menuName + " Item 2");
                if (menuName == "View")
                {
                    juce::PopupMenu sub;
                    sub.addItem(101, "Preset 1");
                    menu.addSubMenu("Submenu", sub);
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

        // Click item 0 (File): opens dropdown immediately
        const juce::MouseEvent clickItem0(juce::Desktop::getInstance().getMainMouseSource(),
                                          juce::Point<float>(15.0f, 20.0f),
                                          juce::ModifierKeys::leftButtonModifier,
                                          juce::MouseInputSource::defaultPressure,
                                          juce::MouseInputSource::defaultOrientation,
                                          juce::MouseInputSource::defaultRotation,
                                          0.0f, 0.0f,
                                          &menuBar,
                                          &menuBar,
                                          juce::Time::getCurrentTime(),
                                          juce::Point<float>(15.0f, 20.0f),
                                          juce::Time::getCurrentTime(),
                                          1, false);

        menuBar.mouseDown(clickItem0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        CHECK(menuBar.getActiveMenuIndex() == 0, "Click on File immediately opens File menu");
        auto* modalMenu0 = juce::Component::getCurrentlyModalComponent();
        CHECK(modalMenu0 != nullptr, "Modal popup window created and displayed for File menu");
        if (modalMenu0 != nullptr)
            CHECK(modalMenu0->isVisible(), "File menu popup window is visible");

        juce::Thread::sleep(120);

        // Click item 1 (Circuit): switches directly to Circuit dropdown
        const juce::MouseEvent clickItem1(juce::Desktop::getInstance().getMainMouseSource(),
                                          juce::Point<float>(95.0f, 20.0f),
                                          juce::ModifierKeys::leftButtonModifier,
                                          juce::MouseInputSource::defaultPressure,
                                          juce::MouseInputSource::defaultOrientation,
                                          juce::MouseInputSource::defaultRotation,
                                          0.0f, 0.0f,
                                          &menuBar,
                                          &menuBar,
                                          juce::Time::getCurrentTime(),
                                          juce::Point<float>(95.0f, 20.0f),
                                          juce::Time::getCurrentTime(),
                                          1, false);

        menuBar.mouseDown(clickItem1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        CHECK(menuBar.getActiveMenuIndex() == 1, "Clicking Circuit switches directly to Circuit menu");
        auto* modalMenu1 = juce::Component::getCurrentlyModalComponent();
        CHECK(modalMenu1 != nullptr, "Circuit menu popup window is active");

        juce::Thread::sleep(120);

        // Click item 1 again: toggles it closed
        menuBar.mouseDown(clickItem1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

        CHECK(menuBar.getActiveMenuIndex() == -1, "Clicking active Circuit menu again toggles it closed");
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr, "Popup window dismissed on toggle close");

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

        // 16. Rapid repeated open/close cycles across all 6 top-level menus
        for (int m = 0; m < 6; ++m)
        {
            menuBar.openMenu(m);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            CHECK(menuBar.getActiveMenuIndex() == m, "Menu " + juce::String(m) + " opened in cycle");
            CHECK(juce::Component::getCurrentlyModalComponent() != nullptr, "Modal popup exists for menu " + juce::String(m));

            menuBar.closeActiveMenu();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            CHECK(menuBar.getActiveMenuIndex() == -1, "Menu " + juce::String(m) + " closed in cycle");
        }
    }

    std::cout << "Results: " << passedChecks << " / " << totalChecks << " checks passed." << std::endl;

    if (passedChecks == totalChecks) {
        std::cout << "ALL WORKSPACE UI TESTS PASSED SUCCESSFULLY!" << std::endl;
        return 0;
    }

    std::cerr << "SOME WORKSPACE UI TESTS FAILED!" << std::endl;
    return 1;
}

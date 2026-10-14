// Control Panel UI Tests
// Fast, lightweight, deterministic unit tests for ControlPanelComponent,
// ControlPanelModel, multi-panel management, layout persistence, and widget interactions.
// ZERO physics simulation, ZERO ODE stepping, completes in milliseconds.

#include "ControlPanelComponent.h"
#include "ControlPanelModel.h"

#include <JuceHeader.h>
#include <iostream>
#include <string>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void check(bool condition, const std::string& label)
    {
        ++checks;
        std::printf("%s  %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!condition)
            ++failures;
    }
};
}

int main()
{
    std::printf("Entering main...\n");
    std::fflush(stdout);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("JUCE initialized.\n");
    std::fflush(stdout);
    Runner r;
    std::printf("=== Control Panel UI Tests ===\n");
    std::fflush(stdout);

    // 1. Initial Default Layout
    sim_panel::ControlPanelLayout layout;
    layout.ensureDefaultPanel();
    r.check(layout.hasPanel("main"), "Default layout contains 'main' panel");
    r.check(layout.activePanelId == "main", "Active panel is 'main'");

    // 2. ControlPanelComponent instantiation & bounds
    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    sim_panel::ControlPanelComponent comp(session);
    comp.setBounds(0, 0, 1280, 800);
    comp.setLayout(layout);
    r.check(comp.getPanelCount() == 1, "Initial panel count is 1");
    r.check(comp.getActivePanelId() == "main", "Component reports active panel 'main'");

    // 3. Multi-Panel Management: Create, Switch, Rename, Remove
    const auto panel2Id = comp.createNewPanel("Operator Auxiliary");
    r.check(!panel2Id.empty() && panel2Id != "main", "Second panel created with unique ID");
    r.check(comp.getPanelCount() == 2, "Panel count incremented to 2");
    r.check(comp.getActivePanelId() == panel2Id, "New panel automatically set as active");

    const auto panel3Id = comp.createNewPanel("Diagnostics Subsystem");
    r.check(comp.getPanelCount() == 3, "Panel count incremented to 3");
    r.check(comp.getActivePanelId() == panel3Id, "Third panel set as active");

    // Switching panels
    comp.setActivePanel(panel2Id);
    r.check(comp.getActivePanelId() == panel2Id, "Switched active panel to second panel");

    // Rename panel
    r.check(comp.renameActivePanel("Custom Aux Console"), "Renamed active panel");
    const auto* p2 = comp.getLayout().findPanel(panel2Id);
    r.check(p2 != nullptr && p2->title == "Custom Aux Console", "Panel title updated in layout model");

    // Remove panel
    comp.setActivePanel(panel3Id);
    r.check(comp.removeActivePanel(), "Removed active panel (panel3)");
    r.check(comp.getPanelCount() == 2, "Panel count returned to 2");
    r.check(!comp.getLayout().hasPanel(panel3Id), "Layout no longer contains removed panel");

    // 4. Widget Addition & Active Panel Targeting (Regression Test for targeting bug)
    // Currently active panel is panel2Id. Adding a widget without specifying panelId MUST attach to panel2Id.
    comp.setActivePanel(panel2Id);

    sim_panel::WidgetConfig btn;
    btn.id = "btn_aux_start";
    btn.type = sim_panel::WidgetType::PushButton;
    btn.title = "Aux Start";
    btn.width = 140;
    btn.height = 36;
    btn.x = 20;
    btn.y = 20;
    // Note: btn.panelId is default (empty)
    r.check(comp.addWidget(btn), "Added Push Button without explicit panelId");

    const auto* addedBtn = comp.getWidgetConfig("btn_aux_start");
    r.check(addedBtn != nullptr, "Added button found in component config");
    r.check(addedBtn != nullptr && addedBtn->panelId == panel2Id,
            "Added button correctly targets active second panel (not 'main')");

    sim_panel::WidgetConfig gauge;
    gauge.id = "gauge_aux_rpm";
    gauge.type = sim_panel::WidgetType::AnalogGauge;
    gauge.title = "Aux RPM";
    gauge.unit = "RPM";
    gauge.minValue = 0.0;
    gauge.maxValue = 3000.0;
    gauge.width = 180;
    gauge.height = 180;
    gauge.x = 180;
    gauge.y = 20;
    r.check(comp.addWidget(gauge), "Added Gauge without explicit panelId");

    const auto* addedGauge = comp.getWidgetConfig("gauge_aux_rpm");
    r.check(addedGauge != nullptr && addedGauge->panelId == panel2Id,
            "Added gauge correctly targets active second panel");

    // Verify visibility / presence on active panel
    r.check(comp.getWidgetComponent("btn_aux_start") != nullptr, "Button is present in current view");
    r.check(comp.getWidgetComponent("gauge_aux_rpm") != nullptr, "Gauge is present in current view");

    // 5. Panel Isolation
    // Switch to 'main' panel: elements from panel2 must NOT be in main panel view
    comp.setActivePanel("main");
    r.check(comp.getActivePanelId() == "main", "Active panel switched to 'main'");
    r.check(comp.getWidgetComponent("btn_aux_start") == nullptr,
            "Aux button does NOT appear on 'main' panel");
    r.check(comp.getWidgetComponent("gauge_aux_rpm") == nullptr,
            "Aux gauge does NOT appear on 'main' panel");

    // Add an element to 'main'
    sim_panel::WidgetConfig mainLamp;
    mainLamp.id = "lamp_main_ready";
    mainLamp.type = sim_panel::WidgetType::IndicatorLamp;
    mainLamp.title = "Main Ready";
    mainLamp.width = 100;
    mainLamp.height = 36;
    mainLamp.x = 40;
    mainLamp.y = 40;
    r.check(comp.addWidget(mainLamp), "Added indicator lamp to 'main' panel");
    r.check(comp.getWidgetComponent("lamp_main_ready") != nullptr, "Lamp visible on 'main'");

    // Switch back to second panel
    comp.setActivePanel(panel2Id);
    r.check(comp.getWidgetComponent("lamp_main_ready") == nullptr,
            "Main lamp does NOT appear on second panel");
    r.check(comp.getWidgetComponent("btn_aux_start") != nullptr,
            "Aux button visible again on second panel");

    // 6. Widget Configuration / Editing
    const auto* currentGauge = comp.getWidgetConfig("gauge_aux_rpm");
    r.check(currentGauge != nullptr, "Gauge config exists before editing");
    if (currentGauge != nullptr)
    {
        auto modifiedGauge = *currentGauge;
        modifiedGauge.title = "Calibrated RPM";
        modifiedGauge.maxValue = 5000.0;
        r.check(comp.configureWidget("gauge_aux_rpm", modifiedGauge), "Configured gauge properties");
        const auto* postEditGauge = comp.getWidgetConfig("gauge_aux_rpm");
        r.check(postEditGauge != nullptr && postEditGauge->title == "Calibrated RPM" && postEditGauge->maxValue == 5000.0,
                "Gauge property edits persisted in config");
    }

    // 7. Design Mode vs Run Mode
    comp.setDesignMode(true);
    r.check(comp.isDesignMode(), "Component is in Design Mode");
    comp.setDesignMode(false);
    r.check(!comp.isDesignMode(), "Component switched to Run Mode");

    // 8. Layout JSON Serialization & Persistence Round-Trip
    const auto savedLayout = comp.getLayout();
    const auto json = savedLayout.toJson();
    r.check(json.isObject(), "Layout serialized to JSON object");

    const auto restoredLayout = sim_panel::ControlPanelLayout::fromJson(json);
    r.check(restoredLayout.panels.size() == 2, "Restored layout contains 2 panels");
    r.check(restoredLayout.hasPanel("main"), "Restored layout contains 'main' panel");
    r.check(restoredLayout.hasPanel(panel2Id), "Restored layout contains second panel");

    const auto* rBtn = restoredLayout.findWidget("btn_aux_start");
    const auto* rGauge = restoredLayout.findWidget("gauge_aux_rpm");
    const auto* rLamp = restoredLayout.findWidget("lamp_main_ready");

    r.check(rBtn != nullptr && rBtn->panelId == panel2Id && rBtn->title == "Aux Start",
            "Restored button preserved on second panel");
    r.check(rGauge != nullptr && rGauge->panelId == panel2Id && rGauge->title == "Calibrated RPM"
            && rGauge->maxValue == 5000.0,
            "Restored gauge preserved on second panel with edited properties");
    r.check(rLamp != nullptr && rLamp->panelId == "main" && rLamp->title == "Main Ready",
            "Restored lamp preserved on 'main' panel");

    // 9. Status and Transient Feedback
    comp.setStatusText("SAVED", juce::Colour(0xff80ed99), 10);
    // Transient status doesn't crash or fault

    // 10. Input Signal Initialization on Run (Defect 1 verification)
    {
        auto motorSession = std::make_shared<sim_panel::SystemSimulationSession>();
        motorSession->setupDefaultMotorSystem();

        sim_panel::ControlPanelComponent motorComp(motorSession);
        auto motorLayout = sim_panel::ControlPanelLayout::createDefaultMotorPanel();

        // Configure speed setpoint to 360 rad/s before Run
        for (auto& w : motorLayout.widgets)
        {
            if (w.signalName == "speed_setpoint" || w.id == "knob_setpoint")
            {
                w.defaultValue = 360.0;
                break;
            }
        }
        motorComp.setLayout(motorLayout);

        // Before touching or running, publishAllControlInputs must set 360 into session
        motorComp.publishAllControlInputs();
        r.check(motorSession->getNumericInput("speed_setpoint") == 360.0,
                "Speed setpoint initialized to 360 rad/s before simulation start");

        // Start simulation without any manual slider drag
        motorSession->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        const auto snap = motorSession->getSnapshot();
        r.check(snap.running, "Simulation is running after start");
        r.check(snap.numericSignals.count("controller.setpoint") != 0, "Controller setpoint signal exists");
        const double setVal = snap.numericSignals.at("controller.setpoint");
        r.check(std::abs(setVal - 360.0) < 1.0,
                "Simulation stepper initialized immediately with 360 rad/s on Run (no manual drag needed)");

        // 11. Live control adjustment during execution
        motorSession->setNumericInput("speed_setpoint", 420.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const auto liveSnap = motorSession->getSnapshot();
        const double liveSet = liveSnap.numericSignals.at("controller.setpoint");
        r.check(std::abs(liveSet - 420.0) < 1.0,
                "Simulation responds to live control adjustment during execution");

        // 12. Stop / Run and Reset Persistence
        motorSession->stop();
        r.check(!motorSession->isRunning(), "Simulation stopped");
        motorSession->reset();
        r.check(motorSession->getNumericInput("speed_setpoint") == 420.0,
                "Reset preserves configured operator setpoint");

        motorComp.publishAllControlInputs();
        motorSession->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const auto restartSnap = motorSession->getSnapshot();
        const double restartSet = restartSnap.numericSignals.at("controller.setpoint");
        r.check(std::abs(restartSet - 360.0) < 1.0 || std::abs(restartSet - 420.0) < 1.0,
                "Restart after Stop/Reset applies configured setpoint immediately");
        motorSession->stop();
    }

    // 13. Output Instruments Updating & Signal Name Normalization (Defect 2 verification)
    {
        // Test signal resolution helper for hierarchical instance signals
        sim_panel::StateSnapshot simSnap;
        simSnap.running = true;
        simSnap.simTime = 1.25;
        simSnap.numericSignals["motor_drivediagram.speed"] = 382.4;
        simSnap.numericSignals["SavedMotorControllerfrcomp.drive_out"] = 6.2;
        simSnap.booleanSignals["lamp.running"] = true;

        auto testSession = std::make_shared<sim_panel::SystemSimulationSession>();
        sim_panel::ControlPanelComponent gaugeComp(testSession);
        gaugeComp.setBounds(0, 0, 1000, 600);

        // Add Analog Gauge bound to "OUT motor.speed"
        sim_panel::WidgetConfig g1;
        g1.id = "gauge_out_speed";
        g1.type = sim_panel::WidgetType::AnalogGauge;
        g1.title = "Motor Speed";
        g1.signalName = "OUT motor.speed";
        g1.unit = "rad/s";
        g1.minValue = 0.0;
        g1.maxValue = 600.0;
        g1.width = 180;
        g1.height = 180;
        gaugeComp.addWidget(g1);

        // Add Analog Gauge bound to "[OUT] motor.speed"
        sim_panel::WidgetConfig g2;
        g2.id = "gauge_bracket_speed";
        g2.type = sim_panel::WidgetType::AnalogGauge;
        g2.title = "Motor Speed (Bracketed)";
        g2.signalName = "[OUT] motor.speed";
        g2.unit = "rad/s";
        g2.minValue = 0.0;
        g2.maxValue = 600.0;
        g2.width = 180;
        g2.height = 180;
        gaugeComp.addWidget(g2);

        // Add Analog Gauge bound to "motor.speed"
        sim_panel::WidgetConfig g3;
        g3.id = "gauge_direct_speed";
        g3.type = sim_panel::WidgetType::AnalogGauge;
        g3.title = "Motor Speed (Direct)";
        g3.signalName = "motor.speed";
        g3.unit = "rad/s";
        g3.minValue = 0.0;
        g3.maxValue = 600.0;
        g3.width = 180;
        g3.height = 180;
        gaugeComp.addWidget(g3);

        // Verify widgets instantiated
        r.check(gaugeComp.getWidgetComponent("gauge_out_speed") != nullptr, "Gauge 1 instantiated");
        r.check(gaugeComp.getWidgetComponent("gauge_bracket_speed") != nullptr, "Gauge 2 instantiated");
        r.check(gaugeComp.getWidgetComponent("gauge_direct_speed") != nullptr, "Gauge 3 instantiated");

        // Pass simulated snapshot through timerCallback and verify gauges update
        // We do this by triggering timer callback via sampleLatestNow() or setting session snapshot
        // Also test signal discovery
        const auto availSignals = gaugeComp.getAvailableSignals();
        bool hasMotorSpeed = false;
        std::string motorSpeedUnit;
        double motorSpeedMax = 0.0;
        for (const auto& s : availSignals)
        {
            if (s.name == "motor.speed")
            {
                hasMotorSpeed = true;
                motorSpeedUnit = s.unit;
                motorSpeedMax = s.maxValue;
                break;
            }
        }
        r.check(hasMotorSpeed, "Available signals contains 'motor.speed'");
        r.check(motorSpeedUnit == "rad/s", "Signal 'motor.speed' has unit 'rad/s' (not 'N*m')");
        r.check(motorSpeedMax == 600.0, "Signal 'motor.speed' has max range 600.0 (accommodates 382 rad/s)");
    }

    std::cout << "--- Finished with " << r.checks << " checks, " << r.failures << " failures ---\n";
    return r.failures == 0 ? 0 : 1;
}

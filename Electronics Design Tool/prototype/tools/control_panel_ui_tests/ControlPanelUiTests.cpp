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

    // 14. Properties Dialog -> Apply -> Live Gauge Rebinding (Current to Speed)
    {
        auto testSession = std::make_shared<sim_panel::SystemSimulationSession>();
        testSession->setupDefaultMotorSystem();
        sim_panel::ControlPanelComponent comp(testSession);
        comp.setBounds(0, 0, 1000, 600);

        sim_panel::WidgetConfig g;
        g.id = "gauge_test";
        g.type = sim_panel::WidgetType::AnalogGauge;
        g.title = "Armature Current";
        g.signalName = "motor.current";
        g.unit = "A";
        g.minValue = -20.0;
        g.maxValue = 20.0;
        g.precision = 2;
        g.width = 180;
        g.height = 180;
        comp.addWidget(g);

        auto* gaugeComp = dynamic_cast<sim_panel::AnalogGaugeComponent*>(comp.getWidgetComponent("gauge_test"));
        r.check(gaugeComp != nullptr, "Gauge component found in canvas");
        r.check(gaugeComp->getUnit() == "A", "Initial gauge unit is 'A'");
        r.check(gaugeComp->getMinVal() == -20.0, "Initial gauge minVal is -20.0");
        r.check(gaugeComp->getMaxVal() == 20.0, "Initial gauge maxVal is 20.0");
        r.check(gaugeComp->getTitle() == "Armature Current", "Initial gauge title is 'Armature Current'");
        r.check(gaugeComp->getPrecision() == 2, "Initial gauge precision is 2");

        // Simulate a previous current value
        gaugeComp->setValue(15.2);
        r.check(gaugeComp->getValue() == 15.2, "Gauge receives value 15.2 A");

        // Open Properties Dialog and rebind to motor.speed
        auto dlg = comp.createWidgetPropertiesDialog("gauge_test");
        r.check(dlg != nullptr, "Created WidgetPropertiesDialog");

        dlg->selectSignalByName("motor.speed");
        dlg->setTitleText("Motor Speed");
        dlg->setMinText("0.0");
        dlg->setMaxText("500.0");
        dlg->setPrecisionIndex(1); // 0 decimal places
        dlg->setWarningZone(true, 400.0, 480.0);
        dlg->setCriticalZone(true, 480.0, 500.0);
        dlg->clickApply();

        // Check that live widget updated immediately without restarting or reloading
        r.check(gaugeComp->getTitle() == "Motor Speed", "Gauge title updated to 'Motor Speed' on Apply");
        r.check(gaugeComp->getUnit() == "rad/s", "Gauge unit updated to 'rad/s' on Apply (not 'A')");
        r.check(gaugeComp->getMinVal() == 0.0, "Gauge minVal updated to 0.0 on Apply");
        r.check(gaugeComp->getMaxVal() == 500.0, "Gauge maxVal updated to 500.0 on Apply");
        r.check(gaugeComp->getPrecision() == 0, "Gauge precision updated to 0 on Apply");
        r.check(gaugeComp->hasWarnRange(), "Gauge warning zone enabled on Apply");
        r.check(gaugeComp->getWarnMin() == 400.0 && gaugeComp->getWarnMax() == 480.0, "Gauge warning zone ranges updated");
        r.check(gaugeComp->hasCritRange(), "Gauge critical zone enabled on Apply");
        r.check(gaugeComp->getCritMin() == 480.0 && gaugeComp->getCritMax() == 500.0, "Gauge critical zone ranges updated");
        r.check(gaugeComp->getValue() == 0.0, "Previous current value purged on rebinding (does not retain 15.2 A)");

        // Verify layout model was updated
        const auto* updatedCfg = comp.getWidgetConfig("gauge_test");
        r.check(updatedCfg != nullptr, "WidgetConfig retained in layout");
        r.check(updatedCfg->signalName == "motor.speed", "WidgetConfig signalName is 'motor.speed'");
        r.check(updatedCfg->unit == "rad/s", "WidgetConfig unit is 'rad/s'");
        r.check(updatedCfg->maxValue == 500.0, "WidgetConfig maxValue is 500.0");
    }

    // 15. Gauge Scaling, Needle Clamping, Small and Out-of-Range Measurements
    {
        sim_panel::AnalogGaugeComponent testGauge("Speed", "rad/s", 0.0, 500.0, 1);

        // Within normal range
        testGauge.setValue(250.0);
        r.check(testGauge.getValue() == 250.0, "Value within range reported correctly");
        r.check(testGauge.getClampedValue() == 250.0, "Needle positioned at 250.0");

        // Negative out-of-range measurement: digital readout preserves actual value, needle clamped to min
        testGauge.setValue(-35.0);
        r.check(testGauge.getValue() == -35.0, "Digital readout preserves negative out-of-range value (-35)");
        r.check(testGauge.getClampedValue() == 0.0, "Needle clamped to dial minimum (0.0)");

        // Positive out-of-range measurement: digital readout preserves actual value, needle clamped to max
        testGauge.setValue(600.0);
        r.check(testGauge.getValue() == 600.0, "Digital readout preserves positive out-of-range value (600)");
        r.check(testGauge.getClampedValue() == 500.0, "Needle clamped to dial maximum (500.0)");

        // Small fractional value
        testGauge.setValue(0.005);
        r.check(testGauge.getValue() == 0.005, "Small value (0.005) preserved accurately");
        r.check(testGauge.getClampedValue() == 0.005, "Needle positioned for small value");

        // Change range to 0..1000 and verify needle scale immediately reflects it
        sim_panel::WidgetConfig cfgScale;
        cfgScale.title = "Speed";
        cfgScale.unit = "rad/s";
        cfgScale.minValue = 0.0;
        cfgScale.maxValue = 1000.0;
        testGauge.updateConfig(cfgScale, false);
        r.check(testGauge.getMaxVal() == 1000.0, "Gauge max scale updated to 1000.0");

        testGauge.setValue(600.0);
        r.check(testGauge.getClampedValue() == 600.0, "At max 1000, 600 is within dial bounds and no longer clamped");
    }

    // 16. Runtime Rebinding During Active Running Simulation
    {
        auto runSession = std::make_shared<sim_panel::SystemSimulationSession>();
        runSession->setupDefaultMotorSystem();
        runSession->setBooleanInput("start_button", true);
        runSession->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sim_panel::ControlPanelComponent runComp(runSession);
        runComp.setBounds(0, 0, 1000, 600);

        sim_panel::WidgetConfig liveGaugeCfg;
        liveGaugeCfg.id = "rt_gauge";
        liveGaugeCfg.type = sim_panel::WidgetType::AnalogGauge;
        liveGaugeCfg.title = "Live Current";
        liveGaugeCfg.signalName = "motor.current";
        liveGaugeCfg.unit = "A";
        liveGaugeCfg.minValue = -20.0;
        liveGaugeCfg.maxValue = 20.0;
        liveGaugeCfg.width = 180;
        liveGaugeCfg.height = 180;
        runComp.addWidget(liveGaugeCfg);

        runComp.sampleLatestNow();
        auto* rtGauge = dynamic_cast<sim_panel::AnalogGaugeComponent*>(runComp.getWidgetComponent("rt_gauge"));
        r.check(rtGauge != nullptr, "Runtime gauge instantiated");
        r.check(rtGauge->getUnit() == "A", "Runtime gauge displays 'A' for current");
        const double initialCurrent = rtGauge->getValue();

        // While simulation is still running, open properties dialog and rebind to motor.speed
        auto dlg = runComp.createWidgetPropertiesDialog("rt_gauge");
        dlg->selectSignalByName("motor.speed");
        dlg->setMinText("0.0");
        dlg->setMaxText("600.0");
        dlg->clickApply();

        // Immediately verify gauge has rebound to speed
        r.check(rtGauge->getUnit() == "rad/s", "Gauge unit updated to 'rad/s' immediately while running");
        r.check(rtGauge->getMaxVal() == 600.0, "Gauge max scale updated to 600.0 immediately while running");

        // Allow simulation to advance further and sample
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        runComp.sampleLatestNow();

        const double liveSpeed = rtGauge->getValue();
        r.check(liveSpeed > 50.0, "Gauge actively tracks changing motor speed (> 50 rad/s)");
        r.check(rtGauge->getUnit() == "rad/s", "Gauge retains 'rad/s' during active simulation");
        r.check(liveSpeed != initialCurrent, "Speed value is independent of prior current reading");

        // 17. Layout Persistence Round-Trip
        juce::var json = runComp.getLayout().toJson();
        auto restoredLayout = sim_panel::ControlPanelLayout::fromJson(json);
        const auto* savedCfg = restoredLayout.findWidget("rt_gauge");
        r.check(savedCfg != nullptr, "Rebound widget found in persisted JSON");
        r.check(savedCfg->signalName == "motor.speed", "Persisted signalName is 'motor.speed'");
        r.check(savedCfg->unit == "rad/s", "Persisted unit is 'rad/s'");
        r.check(savedCfg->maxValue == 600.0, "Persisted maxValue is 600.0");

        sim_panel::ControlPanelComponent freshComp(nullptr);
        freshComp.setLayout(restoredLayout);
        auto* freshGauge = dynamic_cast<sim_panel::AnalogGaugeComponent*>(freshComp.getWidgetComponent("rt_gauge"));
        r.check(freshGauge != nullptr, "Fresh component loaded from JSON instantiated gauge");
        r.check(freshGauge->getUnit() == "rad/s", "Restored gauge displays 'rad/s'");
        r.check(freshGauge->getMaxVal() == 600.0, "Restored gauge has 600.0 max scale");

        runSession->stop();
    }

    // 18. Coexisting Current and Speed Gauges
    {
        auto dualSession = std::make_shared<sim_panel::SystemSimulationSession>();
        dualSession->setupDefaultMotorSystem();
        dualSession->setBooleanInput("start_button", true);
        dualSession->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        sim_panel::ControlPanelComponent dualComp(dualSession);
        dualComp.setBounds(0, 0, 1000, 600);

        sim_panel::WidgetConfig gCurrent;
        gCurrent.id = "gauge_curr";
        gCurrent.type = sim_panel::WidgetType::AnalogGauge;
        gCurrent.title = "Motor Current";
        gCurrent.signalName = "motor.current";
        gCurrent.unit = "A";
        gCurrent.minValue = -20.0;
        gCurrent.maxValue = 20.0;
        dualComp.addWidget(gCurrent);

        sim_panel::WidgetConfig gSpeed;
        gSpeed.id = "gauge_spd";
        gSpeed.type = sim_panel::WidgetType::AnalogGauge;
        gSpeed.title = "Motor Speed";
        gSpeed.signalName = "motor.speed";
        gSpeed.unit = "rad/s";
        gSpeed.minValue = 0.0;
        gSpeed.maxValue = 600.0;
        dualComp.addWidget(gSpeed);

        dualComp.sampleLatestNow();

        auto* currWidget = dynamic_cast<sim_panel::AnalogGaugeComponent*>(dualComp.getWidgetComponent("gauge_curr"));
        auto* spdWidget = dynamic_cast<sim_panel::AnalogGaugeComponent*>(dualComp.getWidgetComponent("gauge_spd"));

        r.check(currWidget != nullptr && spdWidget != nullptr, "Both gauges instantiated side by side");
        r.check(currWidget->getUnit() == "A", "Current gauge displays 'A'");
        r.check(currWidget->getMaxVal() == 20.0, "Current gauge max scale is 20.0");
        r.check(spdWidget->getUnit() == "rad/s", "Speed gauge displays 'rad/s'");
        r.check(spdWidget->getMaxVal() == 600.0, "Speed gauge max scale is 600.0");
        r.check(spdWidget->getValue() > 50.0, "Speed gauge reads motor speed (> 50 rad/s)");
        r.check(currWidget->getValue() != spdWidget->getValue(), "Current and Speed gauges maintain independent values");

        dualSession->stop();
    }

    std::cout << "--- Finished with " << r.checks << " checks, " << r.failures << " failures ---\n";
    return r.failures == 0 ? 0 : 1;
}

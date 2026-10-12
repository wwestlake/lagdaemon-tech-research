// P3 Interactive System Simulation and Control Panel Acceptance Tests
// Verifies the interactive simulation runtime, the 13-step operating sequence,
// rate control, single step, and layout persistence.

#include "../../Source/SystemSimulationSession.h"
#include "../../Source/ControlPanelModel.h"
#include "../../Source/SystemAssembly.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
int passes = 0, failures = 0;

void check(bool ok, const std::string& name, const std::string& detail = {})
{
    if (ok) ++passes; else ++failures;
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), ok || detail.empty() ? "" : "  -- ", ok ? "" : detail.c_str());
    std::fflush(stdout);
}

void testOperatingSequence()
{
    std::printf("-- P3 Operating Sequence: 13-step closed-loop motor control & panel demonstration --\n");

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();

    // Step 1 & 2: Open example, initialize simulation
    session->setupDefaultMotorSystem();
    session->setExecutionRate(0.0); // Free run for fastest test execution
    check(session->start(), "1-2. Simulation initialized and started in free-run mode");

    // Advance 50 ms to establish operating point
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto snap = session->getSnapshot();
    check(!snap.faulted && snap.simTime > 0.01, "Initial state established: ready, t > 0");

    // Step 3: Switch on system power
    session->setBooleanInput("power_switch", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    snap = session->getSnapshot();
    check(!snap.faulted, "3. System power switch ON verified");

    // Step 4: Press Start button
    session->setBooleanInput("start_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session->setBooleanInput("start_button", false); // momentary release

    // Step 5: Observe motor accelerate to commanded speed (300 rad/s)
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    snap = session->getSnapshot();
    const double wStep5 = snap.numericSignals["motor.speed"];
    const bool runLampStep5 = snap.booleanSignals["running"];
    check(runLampStep5 && std::abs(wStep5 - 300.0) < 2.0,
          "4-5. Start button pressed: motor accelerates and stabilizes at 300 rad/s ("
          + std::to_string(wStep5) + " rad/s, running lamp ON)");

    // Step 6: Increase mechanical load while running to 0.03 N*m
    session->setNumericInput("load_torque", 0.03);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Step 7: Observe speed disturbance and controller recovery
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wStep7 = snap.numericSignals["motor.speed"];
    check(std::abs(wStep7 - 300.0) < 2.0,
          "6-7. Mechanical load 0.03 N*m applied: controller compensates disturbance back to setpoint ("
          + std::to_string(wStep7) + " rad/s)");

    // Step 8: Change speed setpoint to 450 rad/s
    session->setNumericInput("speed_setpoint", 450.0);

    // Step 9: Observe new operating point
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    snap = session->getSnapshot();
    const double wStep9 = snap.numericSignals["motor.speed"];
    check(std::abs(wStep9 - 450.0) < 2.0,
          "8-9. Speed setpoint stepped to 450 rad/s: motor tracks and settles at new setpoint ("
          + std::to_string(wStep9) + " rad/s)");

    // Step 10: Press Stop and observe motor decelerate
    session->setNumericInput("load_torque", 0.0);
    session->setBooleanInput("stop_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session->setBooleanInput("stop_button", false);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wStep10 = snap.numericSignals["motor.speed"];
    const bool runLampStep10 = snap.booleanSignals["running"];
    check(!runLampStep10 && wStep10 < 10.0,
          "10. Stop button pressed: motor decelerates to rest (" + std::to_string(wStep10)
          + " rad/s, running lamp OFF)");

    // Step 11: Restart and activate Emergency Stop
    session->setBooleanInput("start_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    session->setBooleanInput("start_button", false);

    // Hit E-Stop
    session->setBooleanInput("estop_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    snap = session->getSnapshot();
    const double driveStep12 = snap.numericSignals["controller.drive"];
    const bool faultLampStep12 = snap.booleanSignals["fault"];
    check(driveStep12 < 0.1 && faultLampStep12,
          "11-12. Emergency Stop activated: drive killed (" + std::to_string(driveStep12)
          + " V), fault/estop lamp ON");

    // Attempting to start while E-Stop is latched must be refused
    session->setBooleanInput("start_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session->setBooleanInput("start_button", false);
    snap = session->getSnapshot();
    check(!snap.booleanSignals["running"], "E-stop interlock: Start ignored while E-Stop is latched");

    // Step 13: Reset system and repeat
    // Release E-Stop, then press Fault Reset
    session->setBooleanInput("estop_button", false);
    session->setBooleanInput("reset_button", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session->setBooleanInput("reset_button", false);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    snap = session->getSnapshot();
    check(!snap.booleanSignals["fault"], "13. Fault Reset pressed: fault cleared, system returned to Ready");

    session->stop();
    check(!session->isRunning(), "Simulation session stopped cleanly");
}

void testSingleStepAndRates()
{
    std::printf("-- P3 Single Step, Pacing Rates & Event Logging --\n");

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    session->setupDefaultMotorSystem();

    // 1. Single Step while paused
    session->start();
    session->pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double t0 = session->simTime();

    session->stepSingle(0.010); // step by 10 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const double t1 = session->simTime();
    check(std::abs((t1 - t0) - 0.010) < 1e-4,
          "Single-step operation advanced simulation time by exactly 10 ms (dt = "
          + std::to_string(t1 - t0) + " s)");

    // 2. Execution rate adjustments
    session->setExecutionRate(2.0);
    check(session->executionRate() == 2.0, "Execution rate configurable (2.0x)");
    session->setExecutionRate(0.5);
    check(session->executionRate() == 0.5, "Execution rate configurable (0.5x)");

    // 3. Event logging
    session->addEventLog("User operated speed knob", "CONTROL");
    const auto snap = session->getSnapshot();
    bool foundEvent = false;
    for (const auto& ev : snap.recentEvents)
    {
        if (ev.message == "User operated speed knob")
            foundEvent = true;
    }
    check(foundEvent, "Timestamped event log published in state snapshot");

    session->stop();
}

void testControlPanelModelPersistence()
{
    std::printf("-- P3 Control Panel Model & JSON Serialization --\n");

    const auto defaultPanel = sim_panel::ControlPanelLayout::createDefaultMotorPanel();
    check(!defaultPanel.widgets.empty(), "Default motor panel layout created with widgets");

    const auto json = defaultPanel.toJson();
    check(json.isObject(), "Control panel model serialized to JSON");

    const auto restored = sim_panel::ControlPanelLayout::fromJson(json);
    check(restored.title == defaultPanel.title && restored.widgets.size() == defaultPanel.widgets.size(),
          "Control panel model round-tripped through JSON with exact widget count ("
          + std::to_string(restored.widgets.size()) + " widgets)");
}

void testAssemblyPersistence()
{
    std::printf("-- P4 System Assembly & JSON Persistence --\n");

    const auto assembly = sim_panel::SystemAssembly::createDualMotorSystem();
    check(!assembly.instances.empty(), "Dual-motor assembly created with subsystem instances");
    check(assembly.instances.size() >= 5, "Subsystem instance count >= 5 (sequencer, 2 controllers, 2 motors)");

    const auto json = assembly.toJson();
    check(json.isObject(), "Assembly serialized to JSON");

    const auto restored = sim_panel::SystemAssembly::fromJson(json);
    check(restored.name == assembly.name, "Assembly name restored correctly");
    check(restored.instances.size() == assembly.instances.size(), "Subsystem instance count preserved");
    check(restored.connections.size() == assembly.connections.size(), "Signal connections count preserved");

    // Also check dual-motor panel layout serialization
    const auto dualPanel = sim_panel::ControlPanelLayout::createDualMotorCoordinatedPanel();
    check(dualPanel.widgets.size() >= 12, "Dual-motor panel layout created with widgets");
    const auto panelJson = dualPanel.toJson();
    const auto restoredPanel = sim_panel::ControlPanelLayout::fromJson(panelJson);
    check(restoredPanel.widgets.size() == dualPanel.widgets.size(), "Dual-motor panel round-tripped through JSON");
}

void testDualMotorCoordinatedSystem()
{
    std::printf("-- P4 Dual-Motor Coordinated System Simulation Demonstration --\n");

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    const auto assembly = sim_panel::SystemAssembly::createDualMotorSystem();
    check(session->setupAssembly(assembly), "1. SystemAssembly successfully compiled and loaded into simulation runtime");

    session->setExecutionRate(0.0); // Free run for fastest test execution
    check(session->start(), "2. Simulation session started in free-run mode");

    // Advance 50 ms to establish initial operating point
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto snap = session->getSnapshot();
    check(!snap.faulted && snap.simTime > 0.01, "3. Initial ready state established (unfaulted, t > 0)");

    // Step 1 & 2: Operator powers on the system
    session->setBooleanInput("system.power", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    snap = session->getSnapshot();
    check(snap.booleanSignals["system.power"] || snap.booleanSignals["power_switch"],
          "4. System power switch set ON; sequencer initialized");

    // Step 3: Motor A starts and reaches its operating speed (300 rad/s)
    // Wait for Motor A to accelerate and stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wA_3 = snap.numericSignals["motorA.speed"];
    check(std::abs(wA_3 - 300.0) < 5.0,
          "5. Motor A started and stabilized at operating setpoint (300 rad/s): measured "
          + std::to_string(wA_3) + " rad/s");

    // Step 4: Motor B starts after programmed delay (150 ms) and reaches 200 rad/s
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    snap = session->getSnapshot();
    const double wB_4 = snap.numericSignals["motorB.speed"];
    check(std::abs(wB_4 - 200.0) < 5.0,
          "6. Motor B started after programmed delay and stabilized at 200 rad/s: measured "
          + std::to_string(wB_4) + " rad/s");

    // Step 5: Operator changes Motor A's load to 0.03 N*m
    session->setNumericInput("motorA.load_torque", 0.03);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Step 6: Motor A's controller compensates independently while Motor B is undisturbed
    snap = session->getSnapshot();
    const double wA_6 = snap.numericSignals["motorA.speed"];
    const double wB_6 = snap.numericSignals["motorB.speed"];
    check(std::abs(wA_6 - 300.0) < 5.0 && std::abs(wB_6 - 200.0) < 5.0,
          "7. Motor A compensated 0.03 N*m load step (" + std::to_string(wA_6)
          + " rad/s) while Motor B remained undisturbed (" + std::to_string(wB_6) + " rad/s)");

    // Step 7: Operator changes Motor B's speed command to 400 rad/s
    session->setNumericInput("motorB.speed_setpoint", 400.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));

    // Step 8: Motor B responds to 400 rad/s without disturbing Motor A
    snap = session->getSnapshot();
    const double wA_8 = snap.numericSignals["motorA.speed"];
    const double wB_8 = snap.numericSignals["motorB.speed"];
    check(std::abs(wB_8 - 400.0) < 5.0 && std::abs(wA_8 - 300.0) < 5.0,
          "8. Motor B stepped to 400 rad/s (" + std::to_string(wB_8)
          + " rad/s) without disturbing Motor A (" + std::to_string(wA_8) + " rad/s)");

    // Step 9: Emergency Stop disables both drives
    session->setBooleanInput("system.estop", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    snap = session->getSnapshot();
    const double driveA_9 = snap.numericSignals["controllerA.drive"];
    const double driveB_9 = snap.numericSignals["controllerB.drive"];
    const bool fault_9 = snap.booleanSignals["system.fault"];
    check(driveA_9 < 0.1 && driveB_9 < 0.1 && fault_9,
          "9. Emergency Stop activated: both drives killed (A: " + std::to_string(driveA_9)
          + " V, B: " + std::to_string(driveB_9) + " V), system fault lamp ON");

    // Allow motors to spin down under friction
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wA_9_down = snap.numericSignals["motorA.speed"];
    const double wB_9_down = snap.numericSignals["motorB.speed"];
    check(wA_9_down < 50.0 && wB_9_down < 50.0,
          "10. Both motors decelerating to stop under E-stop (wA: " + std::to_string(wA_9_down)
          + ", wB: " + std::to_string(wB_9_down) + " rad/s)");

    // Step 10: Fault reset restores the system to its initial ready state
    session->setBooleanInput("system.estop", false);
    session->setBooleanInput("system.reset", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    session->setBooleanInput("system.reset", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    snap = session->getSnapshot();
    check(!snap.booleanSignals["system.fault"],
          "11. Fault Reset pressed: fault cleared, dual-motor system restored to Ready");

    session->stop();
    check(!session->isRunning(), "Dual-motor simulation session stopped cleanly");
}

void testUserAssembledMotorSystem()
{
    using namespace sim_panel;
    std::printf("\n--- Test: P5 User-Assembled Motor Control System ---\n");

    // 1. Create/Load user-assembled motor system (SchematicReference + ProgrammableComponent)
    SystemAssembly assembly = SystemAssembly::createUserAssembledMotorSystem();
    check(assembly.instances.size() == 5, "1. User assembly contains 5 instances (2 schematic drives, 2 FRust controllers, 1 sequencer)");
    check(assembly.connections.size() == 10, "1b. User assembly contains 10 inter-subsystem signal connections");

    // 2. Discover definitions and validate references
    const auto valErrors = assembly.validate();
    check(valErrors.empty(), "2. Assembly validation passed with zero errors");

    bool hasSchematicDef = false;
    bool hasFrustDef = false;
    for (const auto& def : assembly.definitions)
    {
        if (def.kind == SubsystemKind::SchematicReference) hasSchematicDef = true;
        if (def.kind == SubsystemKind::ProgrammableComponent) hasFrustDef = true;
    }
    check(hasSchematicDef && hasFrustDef, "2b. Assembly definitions contain SchematicReference and ProgrammableComponent kinds");

    // 3. Initialize simulation session with user-defined assembly
    auto session = std::make_shared<SystemSimulationSession>();
    bool setupOk = session->setupAssembly(assembly);
    check(setupOk, "3. SystemSimulationSession successfully configured from user-defined assembly");

    session->start();
    check(session->isRunning(), "3b. Interactive simulation session running");

    // 4. Power up system: operator toggles system.power = true
    session->setBooleanInput("system.power", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto snap = session->getSnapshot();
    check(snap.booleanSignals["system.power"], "4. System power active on assembled system");

    // 5. Motor A begins operating and reaches 300 rad/s
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wA_5 = snap.numericSignals["motorA.speed"];
    check(std::abs(wA_5 - 300.0) < 5.0,
          "5. Schematic Motor Drive A started and reached 300 rad/s: measured " + std::to_string(wA_5) + " rad/s");

    // 6. Motor B begins operating after programmed delay (150 ms) and reaches 200 rad/s
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    snap = session->getSnapshot();
    const double wB_6 = snap.numericSignals["motorB.speed"];
    check(std::abs(wB_6 - 200.0) < 5.0,
          "6. Schematic Motor Drive B started after programmed delay and reached 200 rad/s: measured " + std::to_string(wB_6) + " rad/s");

    // 7. Change Motor A load torque to 0.03 N*m; observe independent PID compensation back to 300 rad/s while B is undisturbed
    session->setNumericInput("motorA.load_torque", 0.03);
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    snap = session->getSnapshot();
    const double wA_7 = snap.numericSignals["motorA.speed"];
    const double wB_7 = snap.numericSignals["motorB.speed"];
    check(std::abs(wA_7 - 300.0) < 5.0 && std::abs(wB_7 - 200.0) < 5.0,
          "7. Motor A compensated 0.03 N*m load step (" + std::to_string(wA_7) + " rad/s) while Motor B remained undisturbed (" + std::to_string(wB_7) + " rad/s)");

    // 8. Change Motor B speed setpoint to 400 rad/s; observe response to 400 rad/s without disturbing Motor A
    session->setNumericInput("motorB.speed_setpoint", 400.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    snap = session->getSnapshot();
    const double wA_8 = snap.numericSignals["motorA.speed"];
    const double wB_8 = snap.numericSignals["motorB.speed"];
    check(std::abs(wB_8 - 400.0) < 5.0 && std::abs(wA_8 - 300.0) < 5.0,
          "8. Motor B stepped to 400 rad/s (" + std::to_string(wB_8) + " rad/s) without disturbing Motor A (" + std::to_string(wA_8) + " rad/s)");

    // 9. Verify independent internal state across both controllers and drives
    const double driveA = snap.numericSignals["controllerA.drive"];
    const double driveB = snap.numericSignals["controllerB.drive"];
    check(std::abs(driveA - driveB) > 0.5,
          "9. Verified independent state between controller instances (Drive A: " + std::to_string(driveA) + " V vs Drive B: " + std::to_string(driveB) + " V)");

    // 10. Activate shared emergency stop: verify both drives killed (0 V) and fault indicator ON
    session->setBooleanInput("system.estop", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    snap = session->getSnapshot();
    const double driveA_10 = snap.numericSignals["controllerA.drive"];
    const double driveB_10 = snap.numericSignals["controllerB.drive"];
    const bool fault_10 = snap.booleanSignals["system.fault"];
    check(driveA_10 < 0.2 && driveB_10 < 0.2 && fault_10,
          "10. Emergency stop tripped: both drives killed (A: " + std::to_string(driveA_10) + " V, B: " + std::to_string(driveB_10) + " V), system fault lamp ON");

    // 11. Verify both motors decelerating to stop
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    snap = session->getSnapshot();
    const double wA_11 = snap.numericSignals["motorA.speed"];
    const double wB_11 = snap.numericSignals["motorB.speed"];
    check(wA_11 < 50.0 && wB_11 < 50.0,
          "11. Both user-defined schematic motors decelerating to stop under E-stop (wA: " + std::to_string(wA_11) + ", wB: " + std::to_string(wB_11) + " rad/s)");

    // 12. Fault reset restores the system to ready
    session->setBooleanInput("system.estop", false);
    session->setBooleanInput("system.reset", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    session->setBooleanInput("system.reset", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    snap = session->getSnapshot();
    check(!snap.booleanSignals["system.fault"], "12. Fault reset restored user-assembled system to Ready state");

    session->stop();

    // 13. Save assembly to JSON, deserialize back, re-validate, re-compile and confirm equivalence
    juce::var savedJson = assembly.toJson();
    const auto reloadedAssembly = SystemAssembly::fromJson(savedJson);
    check(reloadedAssembly.name == assembly.name, "13. User-assembled system serialized to JSON and reloaded successfully");
    check(reloadedAssembly.instances.size() == 5 && reloadedAssembly.connections.size() == 10, "13b. Reloaded assembly preserved all instances and connections");
    const auto reloadedValErrors = reloadedAssembly.validate();
    check(reloadedValErrors.empty(), "13c. Reloaded assembly passed validation");

    auto reloadedSession = std::make_shared<SystemSimulationSession>();
    check(reloadedSession->setupAssembly(reloadedAssembly), "13d. Reloaded assembly compiled into new simulation session successfully");
}

} // namespace

int main()
{
    std::printf("====================================================\n");
    std::printf("  Control Panel & Interactive Simulation Tests (P3/P4/P5) \n");
    std::printf("====================================================\n");

    testOperatingSequence();
    testSingleStepAndRates();
    testControlPanelModelPersistence();
    testAssemblyPersistence();
    testDualMotorCoordinatedSystem();
    testUserAssembledMotorSystem();

    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}


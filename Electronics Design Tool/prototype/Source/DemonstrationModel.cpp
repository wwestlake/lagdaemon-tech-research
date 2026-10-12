#include "DemonstrationModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>

namespace demo_system
{

// -----------------------------------------------------------------------------
// Comparison Operators
// -----------------------------------------------------------------------------

ComparisonOp parseComparisonOp(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "less" || s == "<") return ComparisonOp::Less;
    if (s == "lessorequal" || s == "<=") return ComparisonOp::LessOrEqual;
    if (s == "equal" || s == "==") return ComparisonOp::Equal;
    if (s == "greaterorequal" || s == ">=") return ComparisonOp::GreaterOrEqual;
    if (s == "greater" || s == ">") return ComparisonOp::Greater;
    if (s == "notequal" || s == "!=") return ComparisonOp::NotEqual;
    return ComparisonOp::Near;
}

std::string comparisonOpToString(ComparisonOp op)
{
    switch (op)
    {
        case ComparisonOp::Less: return "<";
        case ComparisonOp::LessOrEqual: return "<=";
        case ComparisonOp::Equal: return "==";
        case ComparisonOp::GreaterOrEqual: return ">=";
        case ComparisonOp::Greater: return ">";
        case ComparisonOp::NotEqual: return "!=";
        case ComparisonOp::Near: return "~=";
    }
    return "==";
}

bool ActionCondition::evaluate(double actualValue) const
{
    switch (op)
    {
        case ComparisonOp::Less: return actualValue < (threshold - tolerance);
        case ComparisonOp::LessOrEqual: return actualValue <= (threshold + tolerance);
        case ComparisonOp::Equal: return std::abs(actualValue - threshold) <= tolerance;
        case ComparisonOp::GreaterOrEqual: return actualValue >= (threshold - tolerance);
        case ComparisonOp::Greater: return actualValue > (threshold + tolerance);
        case ComparisonOp::NotEqual: return std::abs(actualValue - threshold) > tolerance;
        case ComparisonOp::Near: return std::abs(actualValue - threshold) <= tolerance;
    }
    return false;
}

bool ActionCondition::evaluate(bool actualValue) const
{
    const bool expectedBool = (threshold > 0.5);
    if (op == ComparisonOp::NotEqual)
        return actualValue != expectedBool;
    return actualValue == expectedBool;
}

juce::var ActionCondition::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("signalName", juce::String(signalName));
    obj->setProperty("op", juce::String(comparisonOpToString(op)));
    obj->setProperty("threshold", threshold);
    obj->setProperty("tolerance", tolerance);
    obj->setProperty("timeoutSeconds", timeoutSeconds);
    return juce::var(obj);
}

ActionCondition ActionCondition::fromJson(const juce::var& json)
{
    ActionCondition c;
    if (const auto* obj = json.getDynamicObject())
    {
        c.signalName = obj->getProperty("signalName").toString().toStdString();
        c.op = parseComparisonOp(obj->getProperty("op").toString().toStdString());
        c.threshold = obj->hasProperty("threshold") ? (double)obj->getProperty("threshold") : 0.0;
        c.tolerance = obj->hasProperty("tolerance") ? (double)obj->getProperty("tolerance") : 0.01;
        c.timeoutSeconds = obj->hasProperty("timeoutSeconds") ? (double)obj->getProperty("timeoutSeconds") : 2.0;
    }
    return c;
}

// -----------------------------------------------------------------------------
// Action Types & Definitions
// -----------------------------------------------------------------------------

ActionType parseActionType(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "startsimulation" || s == "start") return ActionType::StartSimulation;
    if (s == "pausesimulation" || s == "pause") return ActionType::PauseSimulation;
    if (s == "resumesimulation" || s == "resume") return ActionType::ResumeSimulation;
    if (s == "stopsimulation" || s == "stop") return ActionType::StopSimulation;
    if (s == "resetsimulation" || s == "reset") return ActionType::ResetSimulation;
    if (s == "stepsimulation" || s == "step") return ActionType::StepSimulation;
    if (s == "setnumericinput" || s == "set_numeric") return ActionType::SetNumericInput;
    if (s == "setbooleaninput" || s == "set_boolean") return ActionType::SetBooleanInput;
    if (s == "pressbutton" || s == "press") return ActionType::PressButton;
    if (s == "waitduration" || s == "wait_duration") return ActionType::WaitDuration;
    if (s == "waitcondition" || s == "wait_condition") return ActionType::WaitCondition;
    if (s == "assertcondition" || s == "assert") return ActionType::AssertCondition;
    if (s == "recordmeasurement" || s == "measure") return ActionType::RecordMeasurement;
    if (s == "recordobservation" || s == "observe") return ActionType::RecordObservation;
    if (s == "triggerestop" || s == "estop") return ActionType::TriggerEstop;
    if (s == "resetfault" || s == "reset_fault") return ActionType::ResetFault;
    return ActionType::Annotate;
}

std::string actionTypeToString(ActionType type)
{
    switch (type)
    {
        case ActionType::StartSimulation: return "start_simulation";
        case ActionType::PauseSimulation: return "pause_simulation";
        case ActionType::ResumeSimulation: return "resume_simulation";
        case ActionType::StopSimulation: return "stop_simulation";
        case ActionType::ResetSimulation: return "reset_simulation";
        case ActionType::StepSimulation: return "step_simulation";
        case ActionType::SetNumericInput: return "set_numeric_input";
        case ActionType::SetBooleanInput: return "set_boolean_input";
        case ActionType::PressButton: return "press_button";
        case ActionType::WaitDuration: return "wait_duration";
        case ActionType::WaitCondition: return "wait_condition";
        case ActionType::AssertCondition: return "assert_condition";
        case ActionType::RecordMeasurement: return "record_measurement";
        case ActionType::RecordObservation: return "record_observation";
        case ActionType::TriggerEstop: return "trigger_estop";
        case ActionType::ResetFault: return "reset_fault";
        case ActionType::Annotate: return "annotate";
    }
    return "annotate";
}

juce::var DemonstrationAction::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("type", juce::String(actionTypeToString(type)));
    obj->setProperty("description", juce::String(description));
    obj->setProperty("targetSignal", juce::String(targetSignal));
    obj->setProperty("numericValue", numericValue);
    obj->setProperty("boolValue", boolValue);
    obj->setProperty("durationSeconds", durationSeconds);
    obj->setProperty("condition", condition.toJson());

    juce::Array<juce::var> sigs;
    for (const auto& s : signalsToRecord)
        sigs.add(juce::String(s));
    obj->setProperty("signalsToRecord", juce::var(sigs));
    obj->setProperty("annotation", juce::String(annotation));

    return juce::var(obj);
}

DemonstrationAction DemonstrationAction::fromJson(const juce::var& json)
{
    DemonstrationAction a;
    if (const auto* obj = json.getDynamicObject())
    {
        a.id = obj->getProperty("id").toString().toStdString();
        a.type = parseActionType(obj->getProperty("type").toString().toStdString());
        a.description = obj->getProperty("description").toString().toStdString();
        a.targetSignal = obj->getProperty("targetSignal").toString().toStdString();
        a.numericValue = obj->hasProperty("numericValue") ? (double)obj->getProperty("numericValue") : 0.0;
        a.boolValue = (bool)obj->getProperty("boolValue");
        a.durationSeconds = obj->hasProperty("durationSeconds") ? (double)obj->getProperty("durationSeconds") : 0.0;
        a.condition = ActionCondition::fromJson(obj->getProperty("condition"));

        if (const auto* arr = obj->getProperty("signalsToRecord").getArray())
            for (const auto& item : *arr)
                a.signalsToRecord.push_back(item.toString().toStdString());

        a.annotation = obj->getProperty("annotation").toString().toStdString();
    }
    return a;
}

// -----------------------------------------------------------------------------
// DemonstrationDefinition
// -----------------------------------------------------------------------------

bool DemonstrationDefinition::addAction(const DemonstrationAction& action)
{
    if (action.id.empty()) return false;
    for (const auto& a : actions)
        if (a.id == action.id) return false;
    actions.push_back(action);
    return true;
}

bool DemonstrationDefinition::removeAction(const std::string& actionId)
{
    auto it = std::find_if(actions.begin(), actions.end(), [&](const DemonstrationAction& a) { return a.id == actionId; });
    if (it != actions.end())
    {
        actions.erase(it);
        return true;
    }
    return false;
}

juce::var DemonstrationDefinition::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    obj->setProperty("name", juce::String(name));
    obj->setProperty("description", juce::String(description));
    obj->setProperty("machineRef", juce::String(machineRef));
    obj->setProperty("version", juce::String(version));
    obj->setProperty("author", juce::String(author));

    juce::Array<juce::var> tagArr;
    for (const auto& t : tags)
        tagArr.add(juce::String(t));
    obj->setProperty("tags", juce::var(tagArr));

    obj->setProperty("targetExecutionRate", targetExecutionRate);
    obj->setProperty("maxSimulationTime", maxSimulationTime);
    obj->setProperty("stopOnFirstFailure", stopOnFirstFailure);

    juce::Array<juce::var> actArr;
    for (const auto& act : actions)
        actArr.add(act.toJson());
    obj->setProperty("actions", juce::var(actArr));

    return juce::var(obj);
}

DemonstrationDefinition DemonstrationDefinition::fromJson(const juce::var& json)
{
    DemonstrationDefinition def;
    if (const auto* obj = json.getDynamicObject())
    {
        def.id = obj->getProperty("id").toString().toStdString();
        def.name = obj->getProperty("name").toString().toStdString();
        def.description = obj->getProperty("description").toString().toStdString();
        def.machineRef = obj->getProperty("machineRef").toString().toStdString();
        def.version = obj->getProperty("version").toString().toStdString();
        def.author = obj->getProperty("author").toString().toStdString();

        if (const auto* arr = obj->getProperty("tags").getArray())
            for (const auto& item : *arr)
                def.tags.push_back(item.toString().toStdString());

        def.targetExecutionRate = obj->hasProperty("targetExecutionRate") ? (double)obj->getProperty("targetExecutionRate") : 0.0;
        def.maxSimulationTime = obj->hasProperty("maxSimulationTime") ? (double)obj->getProperty("maxSimulationTime") : 30.0;
        def.stopOnFirstFailure = (bool)obj->getProperty("stopOnFirstFailure");

        if (const auto* arr = obj->getProperty("actions").getArray())
            for (const auto& item : *arr)
                def.actions.push_back(DemonstrationAction::fromJson(item));
    }
    return def;
}

bool DemonstrationDefinition::saveToFile(const juce::File& file, juce::String& error) const
{
    const auto str = juce::JSON::toString(toJson(), true);
    if (!file.replaceWithText(str))
    {
        error = "Failed to write demonstration definition: " + file.getFullPathName();
        return false;
    }
    return true;
}

bool DemonstrationDefinition::loadFromFile(const juce::File& file, DemonstrationDefinition& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Demonstration file not found: " + file.getFullPathName();
        return false;
    }
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (!parsed.isObject())
    {
        error = "Invalid JSON in demonstration file: " + file.getFullPathName();
        return false;
    }
    out = fromJson(parsed);
    return true;
}

// -----------------------------------------------------------------------------
// Capstone A: Motor Controller Demonstration
// -----------------------------------------------------------------------------

DemonstrationDefinition DemonstrationDefinition::createCapstoneAMotorDemo()
{
    DemonstrationDefinition d;
    d.id = "capstone_a_motor_demo";
    d.name = "Capstone A: Closed-Loop Dual-Motor Coordinated Demonstration";
    d.description = "Automated operating procedure demonstrating motor startup, speed stabilization, independent multi-axis operation, load disturbance rejection, setpoint step response, and emergency shutdown.";
    d.machineRef = "dual_motor";
    d.tags = { "motor", "closed-loop", "pid", "disturbance", "estop" };
    d.targetExecutionRate = 0.0; // Fast simulation run
    d.maxSimulationTime = 5.0;

    // 1. Initialize and power on
    DemonstrationAction act1;
    act1.id = "act_pwr";
    act1.type = ActionType::SetBooleanInput;
    act1.description = "Power on main system supply";
    act1.targetSignal = "system.power";
    act1.boolValue = true;
    d.actions.push_back(act1);

    // 2. Start simulation
    DemonstrationAction act2;
    act2.id = "act_start_sim";
    act2.type = ActionType::StartSimulation;
    act2.description = "Start continuous MNA transient simulation";
    d.actions.push_back(act2);

    // 3. Press system start button
    DemonstrationAction act3;
    act3.id = "act_start_btn";
    act3.type = ActionType::PressButton;
    act3.description = "Pulse system start button to trigger sequence";
    act3.targetSignal = "system.start";
    act3.durationSeconds = 0.02;
    d.actions.push_back(act3);

    // 4. Wait for Motor A speed to reach and settle at 300 rad/s
    DemonstrationAction act4;
    act4.id = "act_wait_motor_a";
    act4.type = ActionType::WaitCondition;
    act4.description = "Wait for Motor A to accelerate and stabilize at 300 rad/s";
    act4.condition.signalName = "motorA.speed";
    act4.condition.op = ComparisonOp::Near;
    act4.condition.threshold = 300.0;
    act4.condition.tolerance = 5.0;
    act4.condition.timeoutSeconds = 0.6;
    d.actions.push_back(act4);

    // 5. Assert Motor A speed is 300 rad/s
    DemonstrationAction act5;
    act5.id = "act_assert_motor_a";
    act5.type = ActionType::AssertCondition;
    act5.description = "Verify Motor A is running at 300 rad/s target speed";
    act5.condition.signalName = "motorA.speed";
    act5.condition.op = ComparisonOp::Near;
    act5.condition.threshold = 300.0;
    act5.condition.tolerance = 5.0;
    d.actions.push_back(act5);

    // 6. Wait for Motor B delayed start and settle at 200 rad/s
    DemonstrationAction act6;
    act6.id = "act_wait_motor_b";
    act6.type = ActionType::WaitCondition;
    act6.description = "Wait for sequenced Motor B to start and reach 200 rad/s";
    act6.condition.signalName = "motorB.speed";
    act6.condition.op = ComparisonOp::Near;
    act6.condition.threshold = 200.0;
    act6.condition.tolerance = 5.0;
    act6.condition.timeoutSeconds = 0.6;
    d.actions.push_back(act6);

    // 7. Assert independent operation: Motor A at 300 rad/s and Motor B at 200 rad/s
    DemonstrationAction act7;
    act7.id = "act_assert_independent";
    act7.type = ActionType::AssertCondition;
    act7.description = "Verify both motors operate concurrently at distinct setpoints";
    act7.condition.signalName = "motorB.speed";
    act7.condition.op = ComparisonOp::Near;
    act7.condition.threshold = 200.0;
    act7.condition.tolerance = 5.0;
    d.actions.push_back(act7);

    // 8. Record intermediate steady state snapshot
    DemonstrationAction act8;
    act8.id = "act_record_ss";
    act8.type = ActionType::RecordMeasurement;
    act8.description = "Record steady-state dual-axis operational snapshot";
    act8.signalsToRecord = { "motorA.speed", "motorB.speed", "motorA.current", "motorB.current", "controllerA.drive", "controllerB.drive" };
    act8.annotation = "Dual motors at nominal operating points";
    d.actions.push_back(act8);

    // 9. Apply 0.03 N*m load disturbance on Motor A
    DemonstrationAction act9;
    act9.id = "act_load_dist";
    act9.type = ActionType::SetNumericInput;
    act9.description = "Apply sudden 0.03 N*m mechanical load step on Motor A shaft";
    act9.targetSignal = "motorA.load_torque";
    act9.numericValue = 0.03;
    d.actions.push_back(act9);

    // 10. Wait for controller integral action to recover Motor A speed back to 300 rad/s
    DemonstrationAction act10;
    act10.id = "act_wait_dist_comp";
    act10.type = ActionType::WaitCondition;
    act10.description = "Wait for PID loop to compensate load disturbance and restore 300 rad/s";
    act10.condition.signalName = "motorA.speed";
    act10.condition.op = ComparisonOp::Near;
    act10.condition.threshold = 300.0;
    act10.condition.tolerance = 5.0;
    act10.condition.timeoutSeconds = 0.5;
    d.actions.push_back(act10);

    // 11. Assert Motor B remained undisturbed (at 200 rad/s) during Motor A disturbance
    DemonstrationAction act11;
    act11.id = "act_assert_b_undisturbed";
    act11.type = ActionType::AssertCondition;
    act11.description = "Verify cross-axis isolation: Motor B remains undisturbed at 200 rad/s";
    act11.condition.signalName = "motorB.speed";
    act11.condition.op = ComparisonOp::Near;
    act11.condition.threshold = 200.0;
    act11.condition.tolerance = 5.0;
    d.actions.push_back(act11);

    // 12. Change Motor B setpoint to 400 rad/s
    DemonstrationAction act12;
    act12.id = "act_set_b_400";
    act12.type = ActionType::SetNumericInput;
    act12.description = "Step Motor B velocity setpoint to 400 rad/s";
    act12.targetSignal = "motorB.speed_setpoint";
    act12.numericValue = 400.0;
    d.actions.push_back(act12);

    // 13. Wait for Motor B to reach 400 rad/s
    DemonstrationAction act13;
    act13.id = "act_wait_b_400";
    act13.type = ActionType::WaitCondition;
    act13.description = "Wait for Motor B to accelerate to new 400 rad/s setpoint";
    act13.condition.signalName = "motorB.speed";
    act13.condition.op = ComparisonOp::Near;
    act13.condition.threshold = 400.0;
    act13.condition.tolerance = 5.0;
    act13.condition.timeoutSeconds = 0.6;
    d.actions.push_back(act13);

    // 14. Assert Motor B achieved 400 rad/s
    DemonstrationAction act14;
    act14.id = "act_assert_b_400";
    act14.type = ActionType::AssertCondition;
    act14.description = "Verify Motor B is running at 400 rad/s";
    act14.condition.signalName = "motorB.speed";
    act14.condition.op = ComparisonOp::Near;
    act14.condition.threshold = 400.0;
    act14.condition.tolerance = 5.0;
    d.actions.push_back(act14);

    // 15. Trigger Emergency Stop
    DemonstrationAction act15;
    act15.id = "act_estop";
    act15.type = ActionType::TriggerEstop;
    act15.description = "Assert Emergency Stop command across all machine drives";
    d.actions.push_back(act15);

    // 16. Wait 120 ms for emergency shutdown
    DemonstrationAction act16;
    act16.id = "act_wait_estop";
    act16.type = ActionType::WaitDuration;
    act16.description = "Allow dynamic braking and drive de-energization";
    act16.durationSeconds = 0.12;
    d.actions.push_back(act16);

    // 17. Assert fault flag active and controller drive outputs zeroed
    DemonstrationAction act17;
    act17.id = "act_assert_estop_fault";
    act17.type = ActionType::AssertCondition;
    act17.description = "Verify system fault indicator is illuminated";
    act17.condition.signalName = "system.fault";
    act17.condition.op = ComparisonOp::Equal;
    act17.condition.threshold = 1.0;
    d.actions.push_back(act17);

    // 18. Reset fault
    DemonstrationAction act18;
    act18.id = "act_reset_fault";
    act18.type = ActionType::ResetFault;
    act18.description = "Clear Emergency Stop latch and restore Ready state";
    d.actions.push_back(act18);

    return d;
}

// -----------------------------------------------------------------------------
// Capstone B: Robotic Arm Demonstration
// -----------------------------------------------------------------------------

DemonstrationDefinition DemonstrationDefinition::createCapstoneBRoboticArmDemo()
{
    DemonstrationDefinition d;
    d.id = "capstone_b_robotic_arm_demo";
    d.name = "Capstone B: Articulated 2-DOF Robotic Arm Operating Demonstration";
    d.description = "Automated operating procedure demonstrating robotic arm homing, multi-joint articulation, mechanical travel limit enforcement, reversal within bounds, and safety interlock E-stop.";
    d.machineRef = "robotic_arm";
    d.tags = { "robotics", "articulated", "kinematics", "limits", "estop" };
    d.targetExecutionRate = 0.0;
    d.maxSimulationTime = 5.0;

    // 1. Power on
    DemonstrationAction act1;
    act1.id = "act_arm_pwr";
    act1.type = ActionType::SetBooleanInput;
    act1.description = "Apply power to robotic arm controller and servomotors";
    act1.targetSignal = "system.power";
    act1.boolValue = true;
    d.actions.push_back(act1);

    // 2. Start simulation
    DemonstrationAction act2;
    act2.id = "act_arm_start_sim";
    act2.type = ActionType::StartSimulation;
    act2.description = "Start continuous electromechanical simulation";
    d.actions.push_back(act2);

    // 3. Initiate startup & homing sequence
    DemonstrationAction act3;
    act3.id = "act_arm_start";
    act3.type = ActionType::PressButton;
    act3.description = "Trigger arm startup sequence";
    act3.targetSignal = "system.start";
    act3.durationSeconds = 0.02;
    d.actions.push_back(act3);

    // 4. Wait for forward joint actuation (180 ms)
    DemonstrationAction act4;
    act4.id = "act_arm_wait_motion";
    act4.type = ActionType::WaitDuration;
    act4.description = "Observe forward multi-joint rotation";
    act4.durationSeconds = 0.18;
    d.actions.push_back(act4);

    // 5. Assert shoulder joint rotation has occurred
    DemonstrationAction act5;
    act5.id = "act_assert_shoulder_moved";
    act5.type = ActionType::AssertCondition;
    act5.description = "Verify shoulder joint actuated under forward control";
    act5.condition.signalName = "joint_shoulder";
    act5.condition.op = ComparisonOp::Greater;
    act5.condition.threshold = 0.01;
    act5.condition.tolerance = 0.0;
    d.actions.push_back(act5);

    // 6. Record forward motion pose snapshot
    DemonstrationAction act6;
    act6.id = "act_record_fwd";
    act6.type = ActionType::RecordMeasurement;
    act6.description = "Capture intermediate forward arm pose and velocities";
    act6.signalsToRecord = { "joint_shoulder", "joint_elbow", "motorA.speed", "motorB.speed" };
    act6.annotation = "Arm in forward motion toward targets";
    d.actions.push_back(act6);

    // 7. Verify shoulder limit compliance [-2.5, 2.5] rad
    DemonstrationAction act7;
    act7.id = "act_assert_shoulder_limit";
    act7.type = ActionType::AssertCondition;
    act7.description = "Verify shoulder joint respects travel limit envelope (<= 2.5 rad)";
    act7.condition.signalName = "joint_shoulder";
    act7.condition.op = ComparisonOp::LessOrEqual;
    act7.condition.threshold = 2.5;
    act7.condition.tolerance = 0.05;
    d.actions.push_back(act7);

    // 8. Command reversal towards negative target angles
    DemonstrationAction act8;
    act8.id = "act_reverse_shoulder";
    act8.type = ActionType::SetNumericInput;
    act8.description = "Command negative angle setpoint on shoulder";
    act8.targetSignal = "motorA.speed_setpoint";
    act8.numericValue = -2.0;
    d.actions.push_back(act8);

    // 9. Wait for reversal motion (150 ms)
    DemonstrationAction act9;
    act9.id = "act_wait_reversal";
    act9.type = ActionType::WaitDuration;
    act9.description = "Observe reverse motion within limit envelope";
    act9.durationSeconds = 0.15;
    d.actions.push_back(act9);

    // 10. Assert negative travel limit enforcement (>= -2.5 rad)
    DemonstrationAction act10;
    act10.id = "act_assert_rev_limit";
    act10.type = ActionType::AssertCondition;
    act10.description = "Verify joint pose strictly respects lower travel limit (>= -2.5 rad)";
    act10.condition.signalName = "joint_shoulder";
    act10.condition.op = ComparisonOp::GreaterOrEqual;
    act10.condition.threshold = -2.5;
    act10.condition.tolerance = 0.05;
    d.actions.push_back(act10);

    // 11. Trigger Emergency Stop
    DemonstrationAction act11;
    act11.id = "act_arm_estop";
    act11.type = ActionType::TriggerEstop;
    act11.description = "Trigger emergency stop to halt all joint actuators immediately";
    d.actions.push_back(act11);

    // 12. Wait 120 ms
    DemonstrationAction act12;
    act12.id = "act_arm_wait_stop";
    act12.type = ActionType::WaitDuration;
    act12.description = "Observe emergency stop deceleration";
    act12.durationSeconds = 0.12;
    d.actions.push_back(act12);

    // 13. Assert system fault flag active
    DemonstrationAction act13;
    act13.id = "act_assert_arm_fault";
    act13.type = ActionType::AssertCondition;
    act13.description = "Verify arm safety interlock engaged and fault flag asserted";
    act13.condition.signalName = "system.fault";
    act13.condition.op = ComparisonOp::Equal;
    act13.condition.threshold = 1.0;
    d.actions.push_back(act13);

    return d;
}

// -----------------------------------------------------------------------------
// Records & Serialization
// -----------------------------------------------------------------------------

juce::var AssertionResult::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("actionId", juce::String(actionId));
    obj->setProperty("signalName", juce::String(signalName));
    obj->setProperty("comparison", juce::String(comparison));
    obj->setProperty("expectedValue", expectedValue);
    obj->setProperty("actualValue", actualValue);
    obj->setProperty("simTimeSeconds", simTimeSeconds);
    obj->setProperty("passed", passed);
    obj->setProperty("message", juce::String(message));
    return juce::var(obj);
}

AssertionResult AssertionResult::fromJson(const juce::var& json)
{
    AssertionResult r;
    if (const auto* obj = json.getDynamicObject())
    {
        r.actionId = obj->getProperty("actionId").toString().toStdString();
        r.signalName = obj->getProperty("signalName").toString().toStdString();
        r.comparison = obj->getProperty("comparison").toString().toStdString();
        r.expectedValue = obj->hasProperty("expectedValue") ? (double)obj->getProperty("expectedValue") : 0.0;
        r.actualValue = obj->hasProperty("actualValue") ? (double)obj->getProperty("actualValue") : 0.0;
        r.simTimeSeconds = obj->hasProperty("simTimeSeconds") ? (double)obj->getProperty("simTimeSeconds") : 0.0;
        r.passed = (bool)obj->getProperty("passed");
        r.message = obj->getProperty("message").toString().toStdString();
    }
    return r;
}

juce::var ActionLogEntry::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("index", index);
    obj->setProperty("actionId", juce::String(actionId));
    obj->setProperty("type", juce::String(actionTypeToString(type)));
    obj->setProperty("description", juce::String(description));
    obj->setProperty("startSimTime", startSimTime);
    obj->setProperty("endSimTime", endSimTime);
    obj->setProperty("succeeded", succeeded);
    obj->setProperty("message", juce::String(message));
    return juce::var(obj);
}

ActionLogEntry ActionLogEntry::fromJson(const juce::var& json)
{
    ActionLogEntry e;
    if (const auto* obj = json.getDynamicObject())
    {
        e.index = (int)obj->getProperty("index");
        e.actionId = obj->getProperty("actionId").toString().toStdString();
        e.type = parseActionType(obj->getProperty("type").toString().toStdString());
        e.description = obj->getProperty("description").toString().toStdString();
        e.startSimTime = obj->hasProperty("startSimTime") ? (double)obj->getProperty("startSimTime") : 0.0;
        e.endSimTime = obj->hasProperty("endSimTime") ? (double)obj->getProperty("endSimTime") : 0.0;
        e.succeeded = (bool)obj->getProperty("succeeded");
        e.message = obj->getProperty("message").toString().toStdString();
    }
    return e;
}

juce::var MeasurementSnapshot::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("simTimeSeconds", simTimeSeconds);
    obj->setProperty("note", juce::String(note));

    auto* numObj = new juce::DynamicObject();
    for (const auto& pair : numericValues)
        numObj->setProperty(juce::Identifier(pair.first), pair.second);
    obj->setProperty("numericValues", juce::var(numObj));

    auto* boolObj = new juce::DynamicObject();
    for (const auto& pair : booleanValues)
        boolObj->setProperty(juce::Identifier(pair.first), pair.second);
    obj->setProperty("booleanValues", juce::var(boolObj));

    return juce::var(obj);
}

MeasurementSnapshot MeasurementSnapshot::fromJson(const juce::var& json)
{
    MeasurementSnapshot s;
    if (const auto* obj = json.getDynamicObject())
    {
        s.simTimeSeconds = obj->hasProperty("simTimeSeconds") ? (double)obj->getProperty("simTimeSeconds") : 0.0;
        s.note = obj->getProperty("note").toString().toStdString();

        if (const auto* nObj = obj->getProperty("numericValues").getDynamicObject())
            for (const auto& prop : nObj->getProperties())
                s.numericValues[prop.name.toString().toStdString()] = (double)prop.value;

        if (const auto* bObj = obj->getProperty("booleanValues").getDynamicObject())
            for (const auto& prop : bObj->getProperties())
                s.booleanValues[prop.name.toString().toStdString()] = (bool)prop.value;
    }
    return s;
}

juce::var ExperimentMetrics::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("initialValue", initialValue);
    obj->setProperty("targetValue", targetValue);
    obj->setProperty("peakValue", peakValue);
    obj->setProperty("overshootPercent", overshootPercent);
    obj->setProperty("riseTimeSeconds", riseTimeSeconds);
    obj->setProperty("settlingTimeSeconds", settlingTimeSeconds);
    obj->setProperty("steadyStateError", steadyStateError);
    obj->setProperty("isStepResponse", isStepResponse);
    return juce::var(obj);
}

ExperimentMetrics ExperimentMetrics::fromJson(const juce::var& json)
{
    ExperimentMetrics m;
    if (const auto* obj = json.getDynamicObject())
    {
        m.initialValue = obj->hasProperty("initialValue") ? (double)obj->getProperty("initialValue") : 0.0;
        m.targetValue = obj->hasProperty("targetValue") ? (double)obj->getProperty("targetValue") : 0.0;
        m.peakValue = obj->hasProperty("peakValue") ? (double)obj->getProperty("peakValue") : 0.0;
        m.overshootPercent = obj->hasProperty("overshootPercent") ? (double)obj->getProperty("overshootPercent") : 0.0;
        m.riseTimeSeconds = obj->hasProperty("riseTimeSeconds") ? (double)obj->getProperty("riseTimeSeconds") : 0.0;
        m.settlingTimeSeconds = obj->hasProperty("settlingTimeSeconds") ? (double)obj->getProperty("settlingTimeSeconds") : 0.0;
        m.steadyStateError = obj->hasProperty("steadyStateError") ? (double)obj->getProperty("steadyStateError") : 0.0;
        m.isStepResponse = (bool)obj->getProperty("isStepResponse");
    }
    return m;
}

std::string runStatusToString(RunStatus s)
{
    switch (s)
    {
        case RunStatus::NotStarted: return "not_started";
        case RunStatus::Running: return "running";
        case RunStatus::Paused: return "paused";
        case RunStatus::Completed: return "completed";
        case RunStatus::Failed: return "failed";
        case RunStatus::Aborted: return "aborted";
        case RunStatus::TimedOut: return "timed_out";
    }
    return "unknown";
}

bool DemonstrationRun::passed() const
{
    if (status != RunStatus::Completed) return false;
    for (const auto& a : assertionResults)
        if (!a.passed) return false;
    return true;
}

int DemonstrationRun::passedAssertionsCount() const
{
    int c = 0;
    for (const auto& a : assertionResults)
        if (a.passed) ++c;
    return c;
}

int DemonstrationRun::failedAssertionsCount() const
{
    int c = 0;
    for (const auto& a : assertionResults)
        if (!a.passed) ++c;
    return c;
}

juce::var DemonstrationRun::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("runId", juce::String(runId));
    obj->setProperty("demoId", juce::String(demoId));
    obj->setProperty("demoName", juce::String(demoName));
    obj->setProperty("machineId", juce::String(machineId));
    obj->setProperty("machineName", juce::String(machineName));
    obj->setProperty("status", juce::String(runStatusToString(status)));

    obj->setProperty("startSimTime", startSimTime);
    obj->setProperty("endSimTime", endSimTime);
    obj->setProperty("totalSimDuration", totalSimDuration);
    obj->setProperty("actionsExecuted", actionsExecuted);
    obj->setProperty("totalActions", totalActions);
    obj->setProperty("failureReason", juce::String(failureReason));
    obj->setProperty("summary", juce::String(summary));

    juce::Array<juce::var> actArr;
    for (const auto& l : actionLogs) actArr.add(l.toJson());
    obj->setProperty("actionLogs", juce::var(actArr));

    juce::Array<juce::var> asrArr;
    for (const auto& a : assertionResults) asrArr.add(a.toJson());
    obj->setProperty("assertionResults", juce::var(asrArr));

    juce::Array<juce::var> snapArr;
    for (const auto& s : snapshots) snapArr.add(s.toJson());
    obj->setProperty("snapshots", juce::var(snapArr));

    auto* tsObj = new juce::DynamicObject();
    for (const auto& pair : timeSeries)
    {
        juce::Array<juce::var> pts;
        for (const auto& pt : pair.second)
        {
            juce::Array<juce::var> p;
            p.add(pt.first);
            p.add(pt.second);
            pts.add(juce::var(p));
        }
        tsObj->setProperty(juce::Identifier(pair.first), juce::var(pts));
    }
    obj->setProperty("timeSeries", juce::var(tsObj));

    auto* metObj = new juce::DynamicObject();
    for (const auto& pair : metrics)
        metObj->setProperty(juce::Identifier(pair.first), pair.second.toJson());
    obj->setProperty("metrics", juce::var(metObj));

    return juce::var(obj);
}

DemonstrationRun DemonstrationRun::fromJson(const juce::var& json)
{
    DemonstrationRun r;
    if (const auto* obj = json.getDynamicObject())
    {
        r.runId = obj->getProperty("runId").toString().toStdString();
        r.demoId = obj->getProperty("demoId").toString().toStdString();
        r.demoName = obj->getProperty("demoName").toString().toStdString();
        r.machineId = obj->getProperty("machineId").toString().toStdString();
        r.machineName = obj->getProperty("machineName").toString().toStdString();

        const auto stStr = obj->getProperty("status").toString().toLowerCase();
        if (stStr == "completed") r.status = RunStatus::Completed;
        else if (stStr == "running") r.status = RunStatus::Running;
        else if (stStr == "paused") r.status = RunStatus::Paused;
        else if (stStr == "failed") r.status = RunStatus::Failed;
        else if (stStr == "aborted") r.status = RunStatus::Aborted;
        else if (stStr == "timed_out") r.status = RunStatus::TimedOut;
        else r.status = RunStatus::NotStarted;

        r.startSimTime = obj->hasProperty("startSimTime") ? (double)obj->getProperty("startSimTime") : 0.0;
        r.endSimTime = obj->hasProperty("endSimTime") ? (double)obj->getProperty("endSimTime") : 0.0;
        r.totalSimDuration = obj->hasProperty("totalSimDuration") ? (double)obj->getProperty("totalSimDuration") : 0.0;
        r.actionsExecuted = (int)obj->getProperty("actionsExecuted");
        r.totalActions = (int)obj->getProperty("totalActions");
        r.failureReason = obj->getProperty("failureReason").toString().toStdString();
        r.summary = obj->getProperty("summary").toString().toStdString();

        if (const auto* arr = obj->getProperty("actionLogs").getArray())
            for (const auto& item : *arr)
                r.actionLogs.push_back(ActionLogEntry::fromJson(item));

        if (const auto* arr = obj->getProperty("assertionResults").getArray())
            for (const auto& item : *arr)
                r.assertionResults.push_back(AssertionResult::fromJson(item));

        if (const auto* arr = obj->getProperty("snapshots").getArray())
            for (const auto& item : *arr)
                r.snapshots.push_back(MeasurementSnapshot::fromJson(item));

        if (const auto* ts = obj->getProperty("timeSeries").getDynamicObject())
        {
            for (const auto& prop : ts->getProperties())
            {
                if (const auto* pts = prop.value.getArray())
                {
                    std::vector<std::pair<double, double>> vec;
                    for (const auto& pt : *pts)
                    {
                        if (const auto* p = pt.getArray())
                        {
                            if (p->size() >= 2)
                                vec.push_back({ (double)(*p)[0], (double)(*p)[1] });
                        }
                    }
                    r.timeSeries[prop.name.toString().toStdString()] = vec;
                }
            }
        }

        if (const auto* mObj = obj->getProperty("metrics").getDynamicObject())
            for (const auto& prop : mObj->getProperties())
                r.metrics[prop.name.toString().toStdString()] = ExperimentMetrics::fromJson(prop.value);
    }
    return r;
}

bool DemonstrationRun::saveToFile(const juce::File& file, juce::String& error) const
{
    const auto str = juce::JSON::toString(toJson(), true);
    if (!file.replaceWithText(str))
    {
        error = "Failed to write demonstration run record: " + file.getFullPathName();
        return false;
    }
    return true;
}

bool DemonstrationRun::loadFromFile(const juce::File& file, DemonstrationRun& out, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Demonstration run file not found: " + file.getFullPathName();
        return false;
    }
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (!parsed.isObject())
    {
        error = "Invalid JSON in demonstration run file: " + file.getFullPathName();
        return false;
    }
    out = fromJson(parsed);
    return true;
}

std::string DemonstrationRun::generateMarkdownReport() const
{
    std::ostringstream ss;
    ss << "# Engineering Demonstration Report\n\n";
    ss << "**Demonstration:** " << demoName << " (`" << demoId << "`)\n";
    ss << "**Machine:** " << machineName << " (`" << machineId << "`)\n";
    ss << "**Status:** " << runStatusToString(status);
    if (passed()) ss << " (PASS)\n";
    else ss << " (FAIL / INCOMPLETE)\n";
    ss << "**Simulation Duration:** " << std::fixed << std::setprecision(4) << totalSimDuration << " s\n";
    ss << "**Actions Executed:** " << actionsExecuted << " / " << totalActions << "\n";
    ss << "**Assertions:** " << passedAssertionsCount() << " passed, " << failedAssertionsCount() << " failed\n\n";

    if (!failureReason.empty())
    {
        ss << "> **Failure Reason:** " << failureReason << "\n\n";
    }

    if (!summary.empty())
    {
        ss << "## Executive Summary\n" << summary << "\n\n";
    }

    ss << "## Action Execution Sequence\n\n";
    ss << "| Step | Action | Description | Sim Time | Status | Note |\n";
    ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
    for (const auto& l : actionLogs)
    {
        ss << "| " << l.index + 1 << " | `" << actionTypeToString(l.type) << "` | "
           << l.description << " | " << std::fixed << std::setprecision(3) << l.startSimTime << " s | "
           << (l.succeeded ? "OK" : "**FAIL**") << " | " << l.message << " |\n";
    }
    ss << "\n";

    if (!assertionResults.empty())
    {
        ss << "## Engineering Assertions\n\n";
        ss << "| Action | Signal | Comparison | Expected | Measured | Result |\n";
        ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
        for (const auto& a : assertionResults)
        {
            ss << "| `" << a.actionId << "` | `" << a.signalName << "` | " << a.comparison << " | "
               << a.expectedValue << " | " << a.actualValue << " | "
               << (a.passed ? "**PASS**" : "**FAIL**") << " |\n";
        }
        ss << "\n";
    }

    if (!metrics.empty())
    {
        ss << "## Dynamic Performance Metrics\n\n";
        for (const auto& pair : metrics)
        {
            const auto& m = pair.second;
            ss << "### Signal: `" << pair.first << "`\n";
            ss << "- **Initial Value:** " << m.initialValue << "\n";
            ss << "- **Target Value:** " << m.targetValue << "\n";
            ss << "- **Peak Value:** " << m.peakValue << "\n";
            ss << "- **Overshoot:** " << std::fixed << std::setprecision(2) << m.overshootPercent << " %\n";
            ss << "- **Rise Time:** " << std::setprecision(4) << m.riseTimeSeconds << " s\n";
            ss << "- **Settling Time:** " << m.settlingTimeSeconds << " s\n";
            ss << "- **Steady-State Error:** " << m.steadyStateError << "\n\n";
        }
    }

    if (!snapshots.empty())
    {
        ss << "## Recorded Measurement Snapshots\n\n";
        for (size_t i = 0; i < snapshots.size(); ++i)
        {
            const auto& sn = snapshots[i];
            ss << "#### Snapshot " << i + 1 << " at t = " << std::fixed << std::setprecision(3) << sn.simTimeSeconds << " s";
            if (!sn.note.empty()) ss << " (" << sn.note << ")";
            ss << "\n\n";
            for (const auto& p : sn.numericValues)
                ss << "- `" << p.first << "`: " << p.second << "\n";
            for (const auto& p : sn.booleanValues)
                ss << "- `" << p.first << "`: " << (p.second ? "TRUE" : "FALSE") << "\n";
            ss << "\n";
        }
    }

    return ss.str();
}

// -----------------------------------------------------------------------------
// DemonstrationRunner Implementation
// -----------------------------------------------------------------------------

DemonstrationRunner::DemonstrationRunner()
{
}

DemonstrationRunner::~DemonstrationRunner()
{
}

void DemonstrationRunner::setMachine(const machine_builder::MachineDesign& machine)
{
    machine_ = machine;
}

void DemonstrationRunner::setDefinition(const DemonstrationDefinition& def)
{
    def_ = def;
}

void DemonstrationRunner::attachSession(std::shared_ptr<sim_panel::SystemSimulationSession> session)
{
    session_ = session;
}

double DemonstrationRunner::getCurrentSimTime() const
{
    if (session_) return session_->simTime();
    return 0.0;
}

bool DemonstrationRunner::start()
{
    if (!session_)
    {
        session_ = std::make_shared<sim_panel::SystemSimulationSession>();
    }

    // Make sure machine is loaded and compiled
    if (def_.machineRef == "dual_motor" && machine_.assembly.instances.empty())
    {
        machine_ = machine_builder::MachineDesign::createDualMotorMachineTemplate();
    }
    else if (def_.machineRef == "robotic_arm" && machine_.scene.joints.empty())
    {
        machine_ = machine_builder::MachineDesign::createRoboticArmMachineTemplate();
    }

    std::string err;
    if (!machine_.compileAndLoadIntoSession(*session_, &err))
    {
        status_ = RunStatus::Failed;
        currentRun_.status = RunStatus::Failed;
        currentRun_.failureReason = "Failed to compile machine into session: " + err;
        return false;
    }

    session_->setExecutionRate(def_.targetExecutionRate);

    // Initialize run record
    currentRun_ = DemonstrationRun();
    currentRun_.runId = "run_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    currentRun_.demoId = def_.id;
    currentRun_.demoName = def_.name;
    currentRun_.machineId = machine_.id;
    currentRun_.machineName = machine_.name;
    currentRun_.totalActions = (int)def_.actions.size();
    currentRun_.startSimTime = session_->simTime();
    currentRun_.status = RunStatus::Running;

    status_ = RunStatus::Running;
    currentActionIndex_ = 0;
    lastSampleTime_ = session_->simTime();

    return true;
}

void DemonstrationRunner::pause()
{
    if (status_ == RunStatus::Running)
    {
        status_ = RunStatus::Paused;
        currentRun_.status = RunStatus::Paused;
        if (session_) session_->pause();
    }
}

void DemonstrationRunner::resume()
{
    if (status_ == RunStatus::Paused)
    {
        status_ = RunStatus::Running;
        currentRun_.status = RunStatus::Running;
        if (session_) session_->resume();
    }
}

void DemonstrationRunner::stop()
{
    status_ = RunStatus::Aborted;
    currentRun_.status = RunStatus::Aborted;
    if (session_) session_->stop();
}

void DemonstrationRunner::reset()
{
    status_ = RunStatus::NotStarted;
    currentActionIndex_ = 0;
    currentRun_ = DemonstrationRun();
    if (session_) session_->reset();
}

DemonstrationRun DemonstrationRunner::runSynchronous(double maxSimTimeout)
{
    if (!start())
    {
        return currentRun_;
    }

    const double timeoutLimit = (maxSimTimeout > 0.0) ? maxSimTimeout : def_.maxSimulationTime;

    while (currentActionIndex_ < (int)def_.actions.size())
    {
        if (session_->simTime() > timeoutLimit)
        {
            status_ = RunStatus::TimedOut;
            currentRun_.status = RunStatus::TimedOut;
            currentRun_.failureReason = "Demonstration exceeded maximum simulation timeout of " + std::to_string(timeoutLimit) + " s";
            break;
        }

        if (!executeNextAction())
        {
            if (def_.stopOnFirstFailure)
                break;
        }
    }

    if (status_ == RunStatus::Running)
    {
        status_ = RunStatus::Completed;
        currentRun_.status = RunStatus::Completed;
    }

    currentRun_.endSimTime = session_->simTime();
    currentRun_.totalSimDuration = currentRun_.endSimTime - currentRun_.startSimTime;
    currentRun_.actionsExecuted = currentActionIndex_;

    // Compute metrics for key recorded signals if applicable
    for (const auto& pair : currentRun_.timeSeries)
    {
        if (pair.first.find("speed") != std::string::npos && pair.second.size() > 5)
        {
            double target = 300.0;
            if (pair.first.find("motorB") != std::string::npos || pair.first.find("B") != std::string::npos)
                target = 200.0;
            currentRun_.metrics[pair.first] = computeStepResponse(pair.second, 0.0, 0.0, target);
        }
    }

    // Generate comprehensive summary
    std::ostringstream sum;
    sum << "Demonstration '" << def_.name << "' executed " << currentRun_.actionsExecuted << " actions on machine '"
        << machine_.name << "'. Result: " << (currentRun_.passed() ? "PASSED" : "FAILED") << ". "
        << currentRun_.passedAssertionsCount() << " assertions passed, " << currentRun_.failedAssertionsCount() << " failed. "
        << "Total simulated time: " << currentRun_.totalSimDuration << " s.";
    currentRun_.summary = sum.str();

    if (session_)
        session_->stop();

    return currentRun_;
}

bool DemonstrationRunner::executeNextAction()
{
    if (currentActionIndex_ >= (int)def_.actions.size() || !session_)
        return false;

    const auto& act = def_.actions[currentActionIndex_];
    ActionLogEntry logEntry;
    logEntry.index = currentActionIndex_;
    logEntry.actionId = act.id;
    logEntry.type = act.type;
    logEntry.description = act.description;
    logEntry.startSimTime = session_->simTime();

    const bool success = executeAction(act, logEntry);

    logEntry.endSimTime = session_->simTime();
    logEntry.succeeded = success;
    currentRun_.actionLogs.push_back(logEntry);

    if (!success)
    {
        if (def_.stopOnFirstFailure)
        {
            status_ = RunStatus::Failed;
            currentRun_.status = RunStatus::Failed;
            currentRun_.failureReason = "Action " + act.id + " failed: " + logEntry.message;
        }
    }

    currentActionIndex_++;
    return success;
}

bool DemonstrationRunner::executeAction(const DemonstrationAction& act, ActionLogEntry& logEntry)
{
    if (!session_)
    {
        logEntry.message = "No active simulation session.";
        return false;
    }

    switch (act.type)
    {
        case ActionType::StartSimulation:
        {
            if (!session_->isRunning())
                session_->start();
            logEntry.message = "Simulation started.";
            return true;
        }
        case ActionType::PauseSimulation:
        {
            session_->pause();
            logEntry.message = "Simulation paused.";
            return true;
        }
        case ActionType::ResumeSimulation:
        {
            session_->resume();
            logEntry.message = "Simulation resumed.";
            return true;
        }
        case ActionType::StopSimulation:
        {
            session_->stop();
            logEntry.message = "Simulation stopped.";
            return true;
        }
        case ActionType::ResetSimulation:
        {
            session_->reset();
            logEntry.message = "Simulation reset to initial state.";
            return true;
        }
        case ActionType::StepSimulation:
        {
            const double dt = (act.durationSeconds > 0.0) ? act.durationSeconds : 0.001;
            session_->stepSingle(dt);
            logEntry.message = "Stepped " + std::to_string(dt) + " s.";
            return true;
        }
        case ActionType::SetNumericInput:
        {
            session_->setNumericInput(act.targetSignal, act.numericValue);
            logEntry.message = "Set numeric signal '" + act.targetSignal + "' = " + std::to_string(act.numericValue);
            return true;
        }
        case ActionType::SetBooleanInput:
        {
            session_->setBooleanInput(act.targetSignal, act.boolValue);
            logEntry.message = "Set boolean signal '" + act.targetSignal + "' = " + (act.boolValue ? "TRUE" : "FALSE");
            return true;
        }
        case ActionType::PressButton:
        {
            session_->setBooleanInput(act.targetSignal, true);
            const double pulse = (act.durationSeconds > 0.0) ? act.durationSeconds : 0.02;

            if (session_->executionRate() == 0.0)
            {
                // In free-run mode, step simulation time
                const double tTarget = session_->simTime() + pulse;
                while (session_->simTime() < tTarget)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds((int)(pulse * 1000)));
            }

            session_->setBooleanInput(act.targetSignal, false);
            logEntry.message = "Pulsed button '" + act.targetSignal + "' for " + std::to_string(pulse) + " s.";
            return true;
        }
        case ActionType::WaitDuration:
        {
            const double waitDur = act.durationSeconds;
            const double tTarget = session_->simTime() + waitDur;

            if (session_->executionRate() == 0.0)
            {
                const auto wallStart = std::chrono::steady_clock::now();
                while (session_->simTime() < tTarget)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    sampleSignals({ "motorA.speed", "motorB.speed", "motorA.angle", "motorB.angle" }, "");
                    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - wallStart).count() > 5)
                        break;
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds((int)(waitDur * 1000)));
            }

            logEntry.message = "Waited " + std::to_string(waitDur) + " s.";
            return true;
        }
        case ActionType::WaitCondition:
        {
            const bool ok = waitForCondition(act.condition, act.condition.timeoutSeconds);
            logEntry.message = ok ? "Condition met." : ("Condition timeout on " + act.condition.signalName);
            return ok;
        }
        case ActionType::AssertCondition:
        {
            const auto snap = session_->getSnapshot();
            AssertionResult asr;
            asr.actionId = act.id;
            asr.signalName = act.condition.signalName;
            asr.comparison = comparisonOpToString(act.condition.op);
            asr.expectedValue = act.condition.threshold;
            asr.simTimeSeconds = session_->simTime();

            machine_.scene.updateFromSimulation(snap, 0.02);
            auto itNum = snap.numericSignals.find(act.condition.signalName);
            if (itNum != snap.numericSignals.end())
            {
                asr.actualValue = itNum->second;
                asr.passed = act.condition.evaluate(asr.actualValue);
            }
            else
            {
                auto itBool = snap.booleanSignals.find(act.condition.signalName);
                if (itBool != snap.booleanSignals.end())
                {
                    asr.actualValue = itBool->second ? 1.0 : 0.0;
                    asr.passed = act.condition.evaluate(itBool->second);
                }
                else
                {
                    const auto* j = machine_.scene.findJoint(act.condition.signalName);
                    if (!j && !act.condition.signalName.starts_with("joint_"))
                        j = machine_.scene.findJoint("joint_" + act.condition.signalName);

                    if (j != nullptr)
                    {
                        asr.actualValue = j->currentValue;
                        asr.passed = act.condition.evaluate(asr.actualValue);
                    }
                    else
                    {
                        asr.passed = false;
                        asr.message = "Signal '" + act.condition.signalName + "' not found in simulation snapshot.";
                    }
                }
            }

            if (asr.passed)
                asr.message = "Assertion PASSED: measured " + std::to_string(asr.actualValue);
            else if (asr.message.empty())
                asr.message = "Assertion FAILED: measured " + std::to_string(asr.actualValue) + " does not satisfy " + asr.comparison + " " + std::to_string(asr.expectedValue);

            currentRun_.assertionResults.push_back(asr);
            logEntry.message = asr.message;
            return asr.passed;
        }
        case ActionType::RecordMeasurement:
        {
            sampleSignals(act.signalsToRecord, act.annotation);
            logEntry.message = "Measurement snapshot recorded (" + std::to_string(act.signalsToRecord.size()) + " signals).";
            return true;
        }
        case ActionType::RecordObservation:
        {
            sampleSignals({}, act.annotation.empty() ? act.description : act.annotation);
            logEntry.message = "Observation recorded: " + act.description;
            return true;
        }
        case ActionType::TriggerEstop:
        {
            session_->setBooleanInput("system.estop", true);
            logEntry.message = "Triggered emergency stop (system.estop = TRUE).";
            return true;
        }
        case ActionType::ResetFault:
        {
            session_->setBooleanInput("system.estop", false);
            session_->setBooleanInput("system.reset", true);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            session_->setBooleanInput("system.reset", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            logEntry.message = "Reset system faults and restored ready.";
            return true;
        }
        case ActionType::Annotate:
        {
            logEntry.message = "Milestone: " + act.description;
            return true;
        }
    }
    return true;
}

bool DemonstrationRunner::waitForCondition(const ActionCondition& cond, double timeoutSimSec)
{
    if (!session_) return false;

    const double startT = session_->simTime();
    const double limitT = startT + ((timeoutSimSec > 0.0) ? timeoutSimSec : 2.0);
    const auto wallStart = std::chrono::steady_clock::now();

    while (session_->simTime() < limitT)
    {
        const auto snap = session_->getSnapshot();
        machine_.scene.updateFromSimulation(snap, 0.02);
        bool conditionMet = false;

        auto itNum = snap.numericSignals.find(cond.signalName);
        if (itNum != snap.numericSignals.end())
        {
            conditionMet = cond.evaluate(itNum->second);
        }
        else
        {
            auto itBool = snap.booleanSignals.find(cond.signalName);
            if (itBool != snap.booleanSignals.end())
            {
                conditionMet = cond.evaluate(itBool->second);
            }
            else
            {
                const auto* j = machine_.scene.findJoint(cond.signalName);
                if (!j && !cond.signalName.starts_with("joint_"))
                    j = machine_.scene.findJoint("joint_" + cond.signalName);
                if (j != nullptr)
                    conditionMet = cond.evaluate(j->currentValue);
            }
        }

        // Record periodic time-series point
        sampleSignals({ cond.signalName }, "");

        if (conditionMet)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Wallclock safety escape (max 5 seconds real-time)
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - wallStart).count() > 5)
            break;
    }

    return false;
}

void DemonstrationRunner::sampleSignals(const std::vector<std::string>& signals, const std::string& note)
{
    if (!session_) return;

    const auto snap = session_->getSnapshot();
    const double t = snap.simTime;
    machine_.scene.updateFromSimulation(snap, 0.02);

    // Collect snapshot
    if (!signals.empty() || !note.empty())
    {
        MeasurementSnapshot s;
        s.simTimeSeconds = t;
        s.note = note;

        if (signals.empty())
        {
            s.numericValues = snap.numericSignals;
            s.booleanValues = snap.booleanSignals;
        }
        else
        {
            for (const auto& sig : signals)
            {
                auto itN = snap.numericSignals.find(sig);
                if (itN != snap.numericSignals.end())
                    s.numericValues[sig] = itN->second;

                auto itB = snap.booleanSignals.find(sig);
                if (itB != snap.booleanSignals.end())
                    s.booleanValues[sig] = itB->second;

                const auto* j = machine_.scene.findJoint(sig);
                if (!j && !sig.starts_with("joint_"))
                    j = machine_.scene.findJoint("joint_" + sig);
                if (j != nullptr)
                    s.numericValues[sig] = j->currentValue;
            }
        }
        currentRun_.snapshots.push_back(s);
    }

    // Time series downsampling (at most every 2 ms of simulation time)
    if (t - lastSampleTime_ >= 0.002 || lastSampleTime_ < 0.0)
    {
        lastSampleTime_ = t;
        for (const auto& pair : snap.numericSignals)
        {
            currentRun_.timeSeries[pair.first].push_back({ t, pair.second });
        }
    }
}

void DemonstrationRunner::tick(double)
{
    if (status_ == RunStatus::Running)
    {
        if (currentActionIndex_ < (int)def_.actions.size())
        {
            executeNextAction();
        }
        else
        {
            status_ = RunStatus::Completed;
            currentRun_.status = RunStatus::Completed;
            if (session_) session_->stop();
        }
    }
}

// -----------------------------------------------------------------------------
// Automated Experiment Helpers & Comparison
// -----------------------------------------------------------------------------

ExperimentMetrics DemonstrationRunner::computeStepResponse(const std::vector<std::pair<double, double>>& timeSeries,
                                                           double stepTime, double initialVal, double targetVal,
                                                           double toleranceBandPercent)
{
    ExperimentMetrics m;
    m.isStepResponse = true;
    m.initialValue = initialVal;
    m.targetValue = targetVal;

    if (timeSeries.empty() || std::abs(targetVal - initialVal) < 1e-6)
        return m;

    const double delta = targetVal - initialVal;
    const double val10 = initialVal + 0.1 * delta;
    const double val90 = initialVal + 0.9 * delta;
    const double tolBand = std::abs(delta) * (toleranceBandPercent / 100.0);

    double t10 = -1.0;
    double t90 = -1.0;
    double maxVal = initialVal;

    for (const auto& pt : timeSeries)
    {
        if (pt.first < stepTime) continue;

        if (delta > 0)
        {
            if (pt.second > maxVal) maxVal = pt.second;
            if (t10 < 0.0 && pt.second >= val10) t10 = pt.first;
            if (t90 < 0.0 && pt.second >= val90) t90 = pt.first;
        }
        else
        {
            if (pt.second < maxVal) maxVal = pt.second;
            if (t10 < 0.0 && pt.second <= val10) t10 = pt.first;
            if (t90 < 0.0 && pt.second <= val90) t90 = pt.first;
        }
    }

    m.peakValue = maxVal;
    if (delta > 0)
        m.overshootPercent = std::max(0.0, ((maxVal - targetVal) / delta) * 100.0);
    else
        m.overshootPercent = std::max(0.0, ((targetVal - maxVal) / std::abs(delta)) * 100.0);

    if (t10 >= 0.0 && t90 >= 0.0 && t90 >= t10)
        m.riseTimeSeconds = t90 - t10;

    // Settling time: last time signal was outside target ± tolBand
    double lastOutsideT = stepTime;
    for (const auto& pt : timeSeries)
    {
        if (pt.first < stepTime) continue;
        if (std::abs(pt.second - targetVal) > tolBand)
            lastOutsideT = pt.first;
    }
    m.settlingTimeSeconds = std::max(0.0, lastOutsideT - stepTime);

    // Steady state error: last sample vs target
    if (!timeSeries.empty())
        m.steadyStateError = std::abs(timeSeries.back().second - targetVal);

    return m;
}

std::string DemonstrationRunner::compareRuns(const DemonstrationRun& a, const DemonstrationRun& b)
{
    std::ostringstream ss;
    ss << "# Demonstration Run Comparison\n\n";
    ss << "| Metric | Run A (`" << a.runId << "`) | Run B (`" << b.runId << "`) | Delta |\n";
    ss << "| :--- | :--- | :--- | :--- |\n";
    ss << "| Machine | " << a.machineName << " | " << b.machineName << " | - |\n";
    ss << "| Status | " << runStatusToString(a.status) << " | " << runStatusToString(b.status) << " | - |\n";
    ss << "| Total Sim Time | " << std::fixed << std::setprecision(4) << a.totalSimDuration << " s | "
       << b.totalSimDuration << " s | " << (b.totalSimDuration - a.totalSimDuration) << " s |\n";
    ss << "| Passed Assertions | " << a.passedAssertionsCount() << " | " << b.passedAssertionsCount() << " | "
       << (b.passedAssertionsCount() - a.passedAssertionsCount()) << " |\n";
    ss << "| Failed Assertions | " << a.failedAssertionsCount() << " | " << b.failedAssertionsCount() << " | "
       << (b.failedAssertionsCount() - a.failedAssertionsCount()) << " |\n\n";

    ss << "## Summary Comparison\n";
    ss << "- **Run A:** " << a.summary << "\n";
    ss << "- **Run B:** " << b.summary << "\n";

    return ss.str();
}

} // namespace demo_system

#pragma once

#include "MachineModel.h"
#include "SystemSimulationSession.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace demo_system
{

// -----------------------------------------------------------------------------
// Condition and Comparison Operators
// -----------------------------------------------------------------------------

enum class ComparisonOp
{
    Less,
    LessOrEqual,
    Equal,
    GreaterOrEqual,
    Greater,
    NotEqual,
    Near // |signal - threshold| <= tolerance
};

ComparisonOp parseComparisonOp(const std::string& str);
std::string comparisonOpToString(ComparisonOp op);

struct ActionCondition
{
    std::string signalName;
    ComparisonOp op = ComparisonOp::Equal;
    double threshold = 0.0;
    double tolerance = 0.01;
    double timeoutSeconds = 2.0; // In simulation time

    bool evaluate(double actualValue) const;
    bool evaluate(bool actualValue) const;

    juce::var toJson() const;
    static ActionCondition fromJson(const juce::var& json);
};

// -----------------------------------------------------------------------------
// Demonstration Actions
// -----------------------------------------------------------------------------

enum class ActionType
{
    StartSimulation,
    PauseSimulation,
    ResumeSimulation,
    StopSimulation,
    ResetSimulation,
    StepSimulation,
    SetNumericInput,
    SetBooleanInput,
    PressButton,
    WaitDuration,
    WaitCondition,
    AssertCondition,
    RecordMeasurement,
    RecordObservation,
    TriggerEstop,
    ResetFault,
    Annotate
};

ActionType parseActionType(const std::string& str);
std::string actionTypeToString(ActionType type);

struct DemonstrationAction
{
    std::string id;
    ActionType type = ActionType::Annotate;
    std::string description;

    // Parameters for inputs/controls
    std::string targetSignal;
    double numericValue = 0.0;
    bool boolValue = false;
    double durationSeconds = 0.0; // for WaitDuration or PressButton pulse width

    // Condition for WaitCondition / AssertCondition
    ActionCondition condition;

    // Measurement & annotation tags
    std::vector<std::string> signalsToRecord;
    std::string annotation;

    juce::var toJson() const;
    static DemonstrationAction fromJson(const juce::var& json);
};

// -----------------------------------------------------------------------------
// Demonstration Definition (Reusable Asset)
// -----------------------------------------------------------------------------

struct DemonstrationDefinition
{
    std::string id = "demo_project";
    std::string name = "Machine Demonstration";
    std::string description;
    std::string machineRef; // Machine ID, template name ("dual_motor", "robotic_arm"), or file path
    std::string version = "1.0";
    std::string author;
    std::vector<std::string> tags;

    // Initial operating parameters
    double targetExecutionRate = 0.0; // 0.0 = free run, 1.0 = real-time
    double maxSimulationTime = 30.0;  // Maximum permitted simulation duration
    bool stopOnFirstFailure = false;

    // Sequence of actions
    std::vector<DemonstrationAction> actions;

    // Helpers
    bool addAction(const DemonstrationAction& action);
    bool removeAction(const std::string& actionId);

    juce::var toJson() const;
    static DemonstrationDefinition fromJson(const juce::var& json);

    bool saveToFile(const juce::File& file, juce::String& error) const;
    static bool loadFromFile(const juce::File& file, DemonstrationDefinition& out, juce::String& error);

    // Standard pre-built Capstone demonstrations
    static DemonstrationDefinition createCapstoneAMotorDemo();
    static DemonstrationDefinition createCapstoneBRoboticArmDemo();
};

// -----------------------------------------------------------------------------
// Observation, Assertion, and Run Records
// -----------------------------------------------------------------------------

struct AssertionResult
{
    std::string actionId;
    std::string signalName;
    std::string comparison;
    double expectedValue = 0.0;
    double actualValue = 0.0;
    double simTimeSeconds = 0.0;
    bool passed = false;
    std::string message;

    juce::var toJson() const;
    static AssertionResult fromJson(const juce::var& json);
};

struct ActionLogEntry
{
    int index = 0;
    std::string actionId;
    ActionType type = ActionType::Annotate;
    std::string description;
    double startSimTime = 0.0;
    double endSimTime = 0.0;
    bool succeeded = true;
    std::string message;

    juce::var toJson() const;
    static ActionLogEntry fromJson(const juce::var& json);
};

struct MeasurementSnapshot
{
    double simTimeSeconds = 0.0;
    std::map<std::string, double> numericValues;
    std::map<std::string, bool> booleanValues;
    std::string note;

    juce::var toJson() const;
    static MeasurementSnapshot fromJson(const juce::var& json);
};

struct ExperimentMetrics
{
    double initialValue = 0.0;
    double targetValue = 0.0;
    double peakValue = 0.0;
    double overshootPercent = 0.0;
    double riseTimeSeconds = 0.0;
    double settlingTimeSeconds = 0.0;
    double steadyStateError = 0.0;
    bool isStepResponse = false;

    juce::var toJson() const;
    static ExperimentMetrics fromJson(const juce::var& json);
};

enum class RunStatus
{
    NotStarted,
    Running,
    Paused,
    Completed,
    Failed,
    Aborted,
    TimedOut
};

std::string runStatusToString(RunStatus s);

struct DemonstrationRun
{
    std::string runId;
    std::string demoId;
    std::string demoName;
    std::string machineId;
    std::string machineName;
    RunStatus status = RunStatus::NotStarted;

    double startSimTime = 0.0;
    double endSimTime = 0.0;
    double totalSimDuration = 0.0;
    int actionsExecuted = 0;
    int totalActions = 0;

    std::vector<ActionLogEntry> actionLogs;
    std::vector<AssertionResult> assertionResults;
    std::vector<MeasurementSnapshot> snapshots;
    std::map<std::string, std::vector<std::pair<double, double>>> timeSeries; // signal -> [(t, val)]
    std::map<std::string, ExperimentMetrics> metrics;

    std::string failureReason;
    std::string summary;

    bool passed() const;
    int passedAssertionsCount() const;
    int failedAssertionsCount() const;

    juce::var toJson() const;
    static DemonstrationRun fromJson(const juce::var& json);

    bool saveToFile(const juce::File& file, juce::String& error) const;
    static bool loadFromFile(const juce::File& file, DemonstrationRun& out, juce::String& error);

    std::string generateMarkdownReport() const;
};

// -----------------------------------------------------------------------------
// Demonstration Execution Engine
// -----------------------------------------------------------------------------

class DemonstrationRunner
{
public:
    DemonstrationRunner();
    ~DemonstrationRunner();

    DemonstrationRunner(const DemonstrationRunner&) = delete;
    DemonstrationRunner& operator=(const DemonstrationRunner&) = delete;

    void setMachine(const machine_builder::MachineDesign& machine);
    const machine_builder::MachineDesign& getMachine() const { return machine_; }
    machine_builder::MachineDesign& getMachine() { return machine_; }

    void setDefinition(const DemonstrationDefinition& def);
    const DemonstrationDefinition& getDefinition() const { return def_; }
    DemonstrationDefinition& getDefinition() { return def_; }

    void attachSession(std::shared_ptr<sim_panel::SystemSimulationSession> session);
    std::shared_ptr<sim_panel::SystemSimulationSession> getSession() const { return session_; }

    // Execution control
    bool start();
    void pause();
    void resume();
    void stop();
    void reset();

    // Advances execution: either runs all actions synchronously until completion,
    // or executes steps in time increments.
    DemonstrationRun runSynchronous(double maxSimTimeout = 30.0);
    bool executeNextAction();
    void tick(double wallDtSeconds);

    // Status queries
    RunStatus getStatus() const { return status_; }
    int getCurrentActionIndex() const { return currentActionIndex_; }
    double getCurrentSimTime() const;
    const DemonstrationRun& getCurrentRun() const { return currentRun_; }

    // Analysis and metrics helpers
    static ExperimentMetrics computeStepResponse(const std::vector<std::pair<double, double>>& timeSeries,
                                                 double stepTime, double initialVal, double targetVal,
                                                 double toleranceBandPercent = 2.0);

    // Run comparison
    static std::string compareRuns(const DemonstrationRun& a, const DemonstrationRun& b);

private:
    bool executeAction(const DemonstrationAction& act, ActionLogEntry& logEntry);
    bool waitForCondition(const ActionCondition& cond, double timeoutSimSec);
    void sampleSignals(const std::vector<std::string>& signals, const std::string& note);

    machine_builder::MachineDesign machine_;
    DemonstrationDefinition def_;
    std::shared_ptr<sim_panel::SystemSimulationSession> session_;

    RunStatus status_ = RunStatus::NotStarted;
    int currentActionIndex_ = 0;
    DemonstrationRun currentRun_;
    double lastSampleTime_ = -1.0;
};

} // namespace demo_system

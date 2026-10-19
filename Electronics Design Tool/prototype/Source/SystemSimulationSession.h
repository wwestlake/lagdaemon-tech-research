#pragma once

#include "CircuitSolver.h"
#include "Mechanical.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sim_panel
{

enum class DataType
{
    Boolean,
    Numeric,
    Text
};

struct SignalDef
{
    std::string name;
    DataType type = DataType::Numeric;
    bool isOutput = true; // true = simulation output, false = operator input
    std::string unit;
    double minValue = 0.0;
    double maxValue = 100.0;
    std::string description;
    std::string quantity; // "angular_velocity", "angle", "position", "linear_velocity", "voltage", "current", etc.
    std::string kind;     // "state", "rate"
    std::string companionSignal; // explicit declared companion signal (e.g. "motor.angle" for "motor.speed")
};

struct EventItem
{
    double simTime = 0.0;
    std::string message;
    std::string category; // "INFO", "WARN", "FAULT", "CONTROL"
};

// Snapshot of simulation state published to UI
struct StateSnapshot
{
    uint64_t timelineRevision = 0; // changes only when numerical time is reinitialized
    double simTime = 0.0;
    double wallTimeSeconds = 0.0;
    double targetRate = 1.0;
    double actualRate = 1.0;
    bool running = false;
    bool paused = false;
    bool faulted = false;
    std::string faultReason;

    std::map<std::string, double> numericSignals;
    std::map<std::string, bool> booleanSignals;
    std::map<std::string, std::string> textSignals;

    // Simulation-owned integrated motion state (rate-integrated coordinates in SI units)
    std::map<std::string, double> rateIntegrals;

    std::vector<EventItem> recentEvents;
};

// Interactive simulation session managing an execution thread over TransientStepper
class SystemSimulationSession
{
public:
    SystemSimulationSession();
    ~SystemSimulationSession();

    SystemSimulationSession(const SystemSimulationSession&) = delete;
    SystemSimulationSession& operator=(const SystemSimulationSession&) = delete;

    // Configure circuit and bindings
    void setupCircuit(circuit_sim::Circuit circuit,
                      const std::map<std::string, int>& inputElementBindings,
                      const std::map<std::string, circuit_sim::Node>& outputNodeBindings,
                      const std::map<std::string, int>& outputBranchBindings);

    // Setup the closed-loop motor testbench directly
    void setupDefaultMotorSystem();

    // Setup from a user-defined or hierarchical SystemAssembly
    bool setupAssembly(const class SystemAssembly& assembly, const juce::File& projectFolder = {});

    // Simulation lifecycle commands
    bool start(bool initiallyPaused = false);
    bool pause();
    bool resume();
    void stop();
    bool reset();
    bool stepSingle(double dtSeconds = 0.001);

    // Time pacing: 1.0 = real-time, 2.0 = 2x speed, 0.5 = half speed, 0.0 = free run
    void setExecutionRate(double rateFactor);
    double executionRate() const { return targetRate_.load(); }

    // Operator control inputs
    void setNumericInput(const std::string& signalName, double value);
    void setBooleanInput(const std::string& signalName, bool value);
    double getNumericInput(const std::string& signalName) const;
    bool getBooleanInput(const std::string& signalName) const;

    // Observable output query
    StateSnapshot getSnapshot() const;
    void addEventLog(const std::string& message, const std::string& category = "INFO");

    bool isRunning() const { return isRunning_.load(); }
    bool isPaused() const { return isPaused_.load(); }
    double simTime() const { return currentSimTime_.load(); }

    // Inspectable circuit, bindings, and signal catalog
    const circuit_sim::Circuit& baseCircuit() const { return baseCircuit_; }
    const std::map<std::string, int>& inputElementBindings() const { return inputElementBindings_; }
    const std::map<std::string, circuit_sim::Node>& outputNodeBindings() const { return outputNodeBindings_; }
    const std::map<std::string, int>& outputBranchBindings() const { return outputBranchBindings_; }
    const std::vector<SignalDef>& signalDefinitions() const { return signalDefs_; }
    void setSignalDefinitions(std::vector<SignalDef> defs) { signalDefs_ = std::move(defs); }

private:
    void workerThreadLoop();
    int findInputElement(const std::string& signalName) const;
    void applyAllCurrentInputs(circuit_sim::TransientStepper& stepper);
    void applyPendingInputs(circuit_sim::TransientStepper& stepper);
    void publishSnapshot(const circuit_sim::TransientStepper& stepper);

    struct InputCommand
    {
        std::string signalName;
        DataType type = DataType::Numeric;
        double numericVal = 0.0;
        bool boolVal = false;
    };

    circuit_sim::Circuit baseCircuit_;
    std::vector<SignalDef> signalDefs_;
    std::map<std::string, int> inputElementBindings_;
    std::map<std::string, circuit_sim::Node> outputNodeBindings_;
    std::map<std::string, int> outputBranchBindings_;

    std::unique_ptr<std::thread> thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    // Serialize lifecycle callers; completion is signalled by the existing worker.
    std::recursive_mutex lifecycleMutex_;
    bool workerReady_ = false;
    bool workerParked_ = false;
    bool hasStarted_ = false;
    uint64_t requestedStep_ = 0;
    uint64_t completedStep_ = 0;
    bool shouldExit_ = false;

    std::atomic<bool> isRunning_ { false };
    std::atomic<bool> isPaused_ { false };
    std::atomic<bool> isFaulted_ { false };
    std::atomic<double> targetRate_ { 1.0 };
    std::atomic<double> currentSimTime_ { 0.0 };

    bool singleStepRequested_ = false;
    double singleStepDt_ = 0.001;

    std::map<std::string, double> currentNumericInputs_;
    std::map<std::string, bool> currentBooleanInputs_;
    std::deque<InputCommand> pendingInputs_;
    std::vector<EventItem> eventLog_;

    mutable std::mutex snapshotMutex_;
    StateSnapshot latestSnapshot_;

    // Simulation-owned continuous rate integrals across accepted solver steps
    std::map<std::string, double> authoritativeRateIntegrals_;
    double lastSolverSimTime_ = 0.0;

    // Specific variables for the default closed-loop motor system
    int pwrSwitchElem_ = -1;
    int setpointElem_ = -1;
    int loadElem_ = -1;
    circuit_sim::Node speedNode_ = 0;
    circuit_sim::Node angleNode_ = 0;
    circuit_sim::Node driveNode_ = 0;
    int armatureElem_ = -1;
};

} // namespace sim_panel

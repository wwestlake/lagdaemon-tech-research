#include "SystemSimulationSession.h"
#include "SystemAssembly.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sim_panel
{

// A comprehensive integrated controller for the closed-loop motor system
// supporting Start/Stop, E-Stop latching, Fault Reset, Auto/Manual, PID control,
// and status indication.
class IntegratedMotorSystemController final : public circuit_sim::ProgrammableDevice
{
public:
    enum class State
    {
        PowerOff = 0,
        Ready = 1,
        Running = 2,
        Stopped = 3,
        EStopTripped = 4,
        Faulted = 5
    };

    void reset() override
    {
        state = State::PowerOff;
        trialState = state;
        integral = 0.0;
        lastError = 0.0;
        lastT = 0.0;
        trialIntegral = 0.0;
        trialError = 0.0;
        hasLast = false;
        faultCode = 0;
        trialFaultCode = 0;
    }

    bool evaluate(double t, double, const std::vector<double>& v, std::vector<double>& i, std::string&) override
    {
        // Pins:
        // 0: Power sense (V > 2.0 = power ON)
        // 1: Start button sense (V > 2.0 = pressed)
        // 2: Stop button sense (V > 2.0 = pressed)
        // 3: E-Stop sense (V > 2.0 = E-STOP ACTIVE / pressed)
        // 4: Reset button sense (V > 2.0 = pressed)
        // 5: Setpoint input (V = speed_setpoint * 0.01)
        // 6: Tachometer feedback (V = omega * 0.01)
        // 7: Drive output (V -> motor +)
        // 8: Running lamp output (V = 5.0 when running)
        // 9: Fault lamp output (V = 5.0 when faulted / estop)

        const bool pwrOn = (v[0] > 2.0);
        const bool startPressed = (v[1] > 2.0);
        const bool stopPressed = (v[2] > 2.0);
        const bool estopActive = (v[3] > 2.0);
        const bool resetPressed = (v[4] > 2.0);

        const double wTarget = v[5] / 0.01; // target speed in rad/s
        const double wActual = v[6] / 0.01; // actual speed in rad/s

        trialState = state;
        trialFaultCode = faultCode;

        // State Machine transitions
        if (!pwrOn)
        {
            trialState = State::PowerOff;
        }
        else if (estopActive)
        {
            trialState = State::EStopTripped;
            trialFaultCode = 1; // E-Stop
        }
        else
        {
            switch (state)
            {
                case State::PowerOff:
                    trialState = State::Ready;
                    break;

                case State::Ready:
                case State::Stopped:
                    if (startPressed)
                        trialState = State::Running;
                    break;

                case State::Running:
                    if (stopPressed)
                    {
                        trialState = State::Stopped;
                    }
                    break;

                case State::EStopTripped:
                case State::Faulted:
                    if (resetPressed && !estopActive)
                    {
                        trialState = State::Ready;
                        trialFaultCode = 0;
                    }
                    break;
            }
        }

        // Controller drive calculation
        double driveV = 0.0;
        const double dt = (hasLast && t > lastT) ? (t - lastT) : 0.0;
        const double err = wTarget - wActual;

        if (trialState == State::Running)
        {
            const double pTerm = kp * err;
            const double dTerm = (hasLast && dt > 1e-9) ? kd * (err - lastError) / dt : 0.0;

            const double uUnclamped = pTerm + integral * ki + dTerm;
            const bool satHigh = (uUnclamped >= 18.0);
            const bool satLow = (uUnclamped <= 0.0);
            const bool canIntegrate = (!satHigh && !satLow) || (satHigh && err < 0.0) || (satLow && err > 0.0);

            trialIntegral = integral + (canIntegrate ? err * dt : 0.0);
            trialError = err;

            driveV = std::clamp(pTerm + trialIntegral * ki + dTerm, 0.0, 18.0);
        }
        else
        {
            trialIntegral = 0.0;
            trialError = 0.0;
            driveV = 0.0;
        }

        // Outputs
        const double runLampV = (trialState == State::Running) ? 5.0 : 0.0;
        const double faultLampV = (trialState == State::EStopTripped || trialState == State::Faulted) ? 5.0 : 0.0;

        i = {
            -v[0] / 1e6, -v[1] / 1e6, -v[2] / 1e6, -v[3] / 1e6, -v[4] / 1e6,
            -v[5] / 1e6, -v[6] / 1e6,
            (driveV - v[7]) / 0.05,
            (runLampV - v[8]) / 100.0,
            (faultLampV - v[9]) / 100.0
        };

        return true;
    }

    void accept(double t) override
    {
        state = trialState;
        faultCode = trialFaultCode;
        integral = trialIntegral;
        lastError = trialError;
        lastT = t;
        hasLast = true;
    }

    bool saveState(std::vector<std::uint8_t>& out) const override
    {
        out.resize(sizeof(State) + sizeof(int) + sizeof(double) * 3 + sizeof(bool));
        std::uint8_t* p = out.data();
        std::memcpy(p, &state, sizeof(State)); p += sizeof(State);
        std::memcpy(p, &faultCode, sizeof(int)); p += sizeof(int);
        std::memcpy(p, &integral, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &lastError, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &lastT, sizeof(double)); p += sizeof(double);
        std::memcpy(p, &hasLast, sizeof(bool));
        return true;
    }

    bool restoreState(const std::vector<std::uint8_t>& in) override
    {
        if (in.size() < sizeof(State) + sizeof(int) + sizeof(double) * 3 + sizeof(bool)) return false;
        const std::uint8_t* p = in.data();
        std::memcpy(&state, p, sizeof(State)); p += sizeof(State);
        std::memcpy(&faultCode, p, sizeof(int)); p += sizeof(int);
        std::memcpy(&integral, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&lastError, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&lastT, p, sizeof(double)); p += sizeof(double);
        std::memcpy(&hasLast, p, sizeof(bool));
        trialState = state;
        trialFaultCode = faultCode;
        trialIntegral = integral;
        trialError = lastError;
        return true;
    }

    State state = State::PowerOff;
    State trialState = State::PowerOff;
    int faultCode = 0;
    int trialFaultCode = 0;

    double kp = 0.08;
    double ki = 2.0;
    double kd = 0.0002;

    double integral = 0.0;
    double lastError = 0.0;
    double lastT = 0.0;
    double trialIntegral = 0.0;
    double trialError = 0.0;
    bool hasLast = false;
};

SystemSimulationSession::SystemSimulationSession()
{
}

SystemSimulationSession::~SystemSimulationSession()
{
    stop();
}

void SystemSimulationSession::setupCircuit(circuit_sim::Circuit circuit,
                                           const std::map<std::string, int>& inputElementBindings,
                                           const std::map<std::string, circuit_sim::Node>& outputNodeBindings,
                                           const std::map<std::string, int>& outputBranchBindings)
{
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    baseCircuit_ = std::move(circuit);
    inputElementBindings_ = inputElementBindings;
    outputNodeBindings_ = outputNodeBindings;
    outputBranchBindings_ = outputBranchBindings;
    currentSimTime_ = 0.0;
    currentNumericInputs_.clear();
    currentBooleanInputs_.clear();
    pendingInputs_.clear();
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.simTime = 0.0;
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.faulted = false;
        latestSnapshot_.numericSignals.clear();
        latestSnapshot_.booleanSignals.clear();
        latestSnapshot_.textSignals.clear();
        for (const auto& [name, node] : outputNodeBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, elem] : outputBranchBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
    }
}

bool SystemSimulationSession::setupAssembly(const SystemAssembly& assembly, const juce::File& projectFolder)
{
    stop();
    auto compiled = AssemblyCompiler::compile(assembly, projectFolder);
    if (!compiled.success)
    {
        addEventLog("Assembly compilation failed: " + compiled.error, "FAULT");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    baseCircuit_ = std::move(compiled.circuit);
    inputElementBindings_ = std::move(compiled.inputElements);
    outputNodeBindings_ = std::move(compiled.signalNodes);
    outputBranchBindings_ = std::move(compiled.signalBranches);

    signalDefs_.clear();
    for (const auto& meta : compiled.metadata)
    {
        SignalDef def;
        def.name = meta.path;
        def.type = DataType::Numeric;
        def.isOutput = !meta.isInput;
        def.unit = meta.unit;
        def.minValue = meta.minVal;
        def.maxValue = meta.maxVal;
        def.description = meta.description.empty() ? meta.path : meta.description;
        def.quantity = meta.quantity;
        def.kind = meta.kind;
        signalDefs_.push_back(def);
    }

    currentSimTime_ = 0.0;
    currentNumericInputs_.clear();
    currentBooleanInputs_.clear();
    pendingInputs_.clear();

    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.simTime = 0.0;
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.faulted = false;
        latestSnapshot_.numericSignals.clear();
        latestSnapshot_.booleanSignals.clear();
        latestSnapshot_.textSignals.clear();
        for (const auto& [name, node] : outputNodeBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, elem] : outputBranchBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, val] : currentNumericInputs_)
            latestSnapshot_.numericSignals[name] = val;
        for (const auto& [name, val] : currentBooleanInputs_)
            latestSnapshot_.booleanSignals[name] = val;
    }

    addEventLog("System Assembly Loaded: " + assembly.name, "INFO");
    return true;
}

void SystemSimulationSession::setupDefaultMotorSystem()
{
    stop();
    std::lock_guard<std::mutex> lock(mutex_);

    circuit_sim::Circuit c;

    // Nodes
    const auto pwrSenseNode = c.addNode();
    const auto startNode = c.addNode();
    const auto stopNode = c.addNode();
    const auto estopNode = c.addNode();
    const auto resetNode = c.addNode();
    const auto setpointNode = c.addNode();
    const auto tachNode = c.addNode();
    const auto driveNode = c.addNode();
    const auto runLampNode = c.addNode();
    const auto faultLampNode = c.addNode();
    const auto posNode = c.addNode();

    // Independent control inputs
    circuit_sim::Waveform dc0; dc0.offset = 0.0;
    circuit_sim::Waveform dc1; dc1.offset = 5.0; // Power starts on
    circuit_sim::Waveform dcSet; dcSet.offset = 3.0; // 300 rad/s initial setpoint

    pwrSwitchElem_ = c.addVoltageSource("V_PWR", pwrSenseNode, 0, dc1);
    const int startElem = c.addVoltageSource("V_START", startNode, 0, dc0);
    const int stopElem = c.addVoltageSource("V_STOP", stopNode, 0, dc0);
    const int estopElem = c.addVoltageSource("V_ESTOP", estopNode, 0, dc0);
    const int resetElem = c.addVoltageSource("V_RESET", resetNode, 0, dc0);
    setpointElem_ = c.addVoltageSource("V_SETPOINT", setpointNode, 0, dcSet);

    // Controller
    auto ctl = std::make_shared<IntegratedMotorSystemController>();
    c.addProgrammable("U_CONTROLLER",
                     { pwrSenseNode, startNode, stopNode, estopNode, resetNode,
                       setpointNode, tachNode, driveNode, runLampNode, faultLampNode },
                     ctl);

    // Motor
    mech::DcMotorModel m;
    mech::MotorLoad load;
    mech::ShaftModel shaft;
    const auto motor = mech::addDcMotor(c, "M1", driveNode, 0, m, load, shaft);
    mech::addTachometer(c, "TG", motor.shaft, tachNode, 0, 0.01, 100.0);
    mech::addPositionSensor(c, "ENC", motor.angle, posNode, 0, 1.0, 100.0);

    // Mechanical load torque source
    loadElem_ = c.addCurrentSource("TL_LOAD", motor.shaft, 0, dc0);

    // Lamps
    c.addResistor("R_RUN_LAMP", runLampNode, 0, 1000.0);
    c.addResistor("R_FAULT_LAMP", faultLampNode, 0, 1000.0);

    // Store element & node references
    speedNode_ = motor.shaft;
    angleNode_ = motor.angle;
    driveNode_ = driveNode;
    armatureElem_ = motor.armature;

    baseCircuit_ = std::move(c);

    inputElementBindings_["power_switch"] = pwrSwitchElem_;
    inputElementBindings_["start_button"] = startElem;
    inputElementBindings_["stop_button"] = stopElem;
    inputElementBindings_["estop_button"] = estopElem;
    inputElementBindings_["reset_button"] = resetElem;
    inputElementBindings_["speed_setpoint"] = setpointElem_;
    inputElementBindings_["load_torque"] = loadElem_;

    outputNodeBindings_["motor.speed"] = speedNode_;
    outputNodeBindings_["motor.angle"] = angleNode_;
    outputNodeBindings_["controller.drive"] = driveNode_;
    outputNodeBindings_["lamp.running"] = runLampNode;
    outputNodeBindings_["lamp.fault"] = faultLampNode;
    outputNodeBindings_["controller.setpoint"] = setpointNode;

    outputBranchBindings_["motor.current"] = armatureElem_;

    signalDefs_.clear();
    signalDefs_.push_back({ "motor.speed", DataType::Numeric, true, "rad/s", 0.0, 500.0, "Motor Shaft Angular Velocity", "angular_velocity", "rate" });
    signalDefs_.push_back({ "motor.angle", DataType::Numeric, true, "rad", -1e6, 1e6, "Motor Shaft Angular Position", "angle", "state" });
    signalDefs_.push_back({ "motor.current", DataType::Numeric, true, "A", -50.0, 50.0, "Motor Armature Current", "current", "state" });
    signalDefs_.push_back({ "controller.drive", DataType::Numeric, true, "V", -24.0, 24.0, "Controller Output Voltage", "voltage", "state" });
    signalDefs_.push_back({ "controller.setpoint", DataType::Numeric, true, "rad/s", 0.0, 500.0, "Speed Setpoint", "angular_velocity", "state" });
    signalDefs_.push_back({ "lamp.running", DataType::Boolean, true, "bool", 0.0, 1.0, "Running Status Indicator", "status", "state" });
    signalDefs_.push_back({ "lamp.fault", DataType::Boolean, true, "bool", 0.0, 1.0, "Fault Status Indicator", "status", "state" });
    signalDefs_.push_back({ "speed_setpoint", DataType::Numeric, false, "rad/s", 0.0, 500.0, "Target Speed Control", "angular_velocity", "state" });
    signalDefs_.push_back({ "load_torque", DataType::Numeric, false, "N*m", 0.0, 2.0, "Mechanical Load Torque", "torque", "state" });
    signalDefs_.push_back({ "power_switch", DataType::Boolean, false, "bool", 0.0, 1.0, "System Power Switch", "control", "state" });
    signalDefs_.push_back({ "start_button", DataType::Boolean, false, "bool", 0.0, 1.0, "Start Cycle Button", "control", "state" });
    signalDefs_.push_back({ "stop_button", DataType::Boolean, false, "bool", 0.0, 1.0, "Stop Cycle Button", "control", "state" });
    signalDefs_.push_back({ "estop_button", DataType::Boolean, false, "bool", 0.0, 1.0, "Emergency Stop Button", "control", "state" });
    signalDefs_.push_back({ "reset_button", DataType::Boolean, false, "bool", 0.0, 1.0, "Fault Reset Button", "control", "state" });

    currentNumericInputs_["speed_setpoint"] = 300.0;
    currentNumericInputs_["load_torque"] = 0.0;
    currentBooleanInputs_["power_switch"] = true;
    currentBooleanInputs_["start_button"] = false;
    currentBooleanInputs_["stop_button"] = false;
    currentBooleanInputs_["estop_button"] = false;
    currentBooleanInputs_["reset_button"] = false;
    pendingInputs_.clear();

    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.simTime = 0.0;
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.faulted = false;
        latestSnapshot_.numericSignals.clear();
        latestSnapshot_.booleanSignals.clear();
        latestSnapshot_.textSignals.clear();
        for (const auto& [name, node] : outputNodeBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, elem] : outputBranchBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        latestSnapshot_.numericSignals["speed_setpoint"] = 300.0;
        latestSnapshot_.numericSignals["controller.setpoint"] = 300.0;
        latestSnapshot_.numericSignals["motor.speed"] = 0.0;
        latestSnapshot_.numericSignals["speed"] = 0.0;
        latestSnapshot_.booleanSignals["power_switch"] = true;
    }

    addEventLog("System Initialized: Ready", "INFO");
}

bool SystemSimulationSession::start()
{
    if (isRunning_.load())
    {
        if (isPaused_.load())
        {
            resume();
            return true;
        }
        return true;
    }

    shouldExit_ = false;
    isPaused_ = false;
    singleStepRequested_ = false;

    thread_ = std::make_unique<std::thread>(&SystemSimulationSession::workerThreadLoop, this);
    isRunning_ = true;

    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.running = true;
        latestSnapshot_.paused = false;
    }

    addEventLog("Simulation session started", "CONTROL");
    return true;
}

void SystemSimulationSession::pause()
{
    isPaused_ = true;
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.paused = true;
    }
    addEventLog("Simulation paused", "CONTROL");
}

void SystemSimulationSession::resume()
{
    isPaused_ = false;
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.paused = false;
    }
    cv_.notify_all();
    addEventLog("Simulation resumed", "CONTROL");
}

void SystemSimulationSession::stop()
{
    if (!isRunning_.load() && !thread_)
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        shouldExit_ = true;
    }
    cv_.notify_all();

    if (thread_ && thread_->joinable())
    {
        thread_->join();
        thread_.reset();
    }

    isRunning_ = false;
    isPaused_ = false;
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.simTime = currentSimTime_.load();
    }
    addEventLog("Simulation stopped", "CONTROL");
}

void SystemSimulationSession::reset()
{
    stop();
    currentSimTime_ = 0.0;
    isFaulted_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingInputs_.clear();
    }
    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        latestSnapshot_.simTime = 0.0;
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.faulted = false;
        latestSnapshot_.numericSignals.clear();
        latestSnapshot_.booleanSignals.clear();
        latestSnapshot_.textSignals.clear();
    }
    addEventLog("Simulation reset to t = 0.000s", "CONTROL");
}

void SystemSimulationSession::stepSingle(double dtSeconds)
{
    if (!isRunning_.load())
    {
        start();
        pause();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    singleStepRequested_ = true;
    singleStepDt_ = dtSeconds;
    cv_.notify_all();
}

void SystemSimulationSession::setExecutionRate(double rateFactor)
{
    targetRate_ = std::max(0.0, rateFactor);
    addEventLog("Execution rate set to " + std::to_string(targetRate_.load()) + "x", "CONTROL");
}

void SystemSimulationSession::setNumericInput(const std::string& signalName, double value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    currentNumericInputs_[signalName] = value;
    pendingInputs_.push_back({ signalName, DataType::Numeric, value, false });
}

void SystemSimulationSession::setBooleanInput(const std::string& signalName, bool value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    currentBooleanInputs_[signalName] = value;
    pendingInputs_.push_back({ signalName, DataType::Boolean, 0.0, value });
}

double SystemSimulationSession::getNumericInput(const std::string& signalName) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = currentNumericInputs_.find(signalName);
    if (it != currentNumericInputs_.end())
        return it->second;

    for (const auto& [k, v] : currentNumericInputs_)
    {
        if (k.find(signalName) != std::string::npos || signalName.find(k) != std::string::npos)
            return v;
    }
    return 0.0;
}

bool SystemSimulationSession::getBooleanInput(const std::string& signalName) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = currentBooleanInputs_.find(signalName);
    if (it != currentBooleanInputs_.end())
        return it->second;

    for (const auto& [k, v] : currentBooleanInputs_)
    {
        if (k.find(signalName) != std::string::npos || signalName.find(k) != std::string::npos)
            return v;
    }
    return false;
}

int SystemSimulationSession::findInputElement(const std::string& signalName) const
{
    if (signalName.empty()) return -1;

    // 1. Direct exact match
    auto it = inputElementBindings_.find(signalName);
    if (it != inputElementBindings_.end())
        return it->second;

    // Helper to sanitize
    std::string clean = signalName;
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '\t')) clean.pop_back();

    auto stripPfx = [&](const std::string& pfx) {
        if (clean.size() >= pfx.size())
        {
            bool m = true;
            for (size_t i = 0; i < pfx.size(); ++i)
            {
                if (std::tolower((unsigned char)clean[i]) != std::tolower((unsigned char)pfx[i]))
                {
                    m = false; break;
                }
            }
            if (m) {
                clean = clean.substr(pfx.size());
                while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t')) clean.erase(clean.begin());
                return true;
            }
        }
        return false;
    };
    stripPfx("[in]"); stripPfx("in:"); stripPfx("in ");

    it = inputElementBindings_.find(clean);
    if (it != inputElementBindings_.end())
        return it->second;

    // 2. Case-insensitive match
    std::string cleanLower = clean;
    for (char& c : cleanLower) c = (char)std::tolower((unsigned char)c);

    for (const auto& [k, elem] : inputElementBindings_)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        if (kLower == cleanLower)
            return elem;
    }

    // 3. Suffix match (e.g. "speed_setpoint" matching "system.speed_setpoint" or vice versa)
    const auto dotPos = cleanLower.rfind('.');
    const std::string suffix = (dotPos != std::string::npos) ? cleanLower.substr(dotPos + 1) : cleanLower;

    for (const auto& [k, elem] : inputElementBindings_)
    {
        std::string kLower = k;
        for (char& c : kLower) c = (char)std::tolower((unsigned char)c);
        const auto kDot = kLower.rfind('.');
        const std::string kSuff = (kDot != std::string::npos) ? kLower.substr(kDot + 1) : kLower;
        if (kSuff == suffix || (kLower.rfind("." + suffix) != std::string::npos))
            return elem;
    }

    // 4. Try prefixing "system."
    std::string sysClean = "system." + clean;
    it = inputElementBindings_.find(sysClean);
    if (it != inputElementBindings_.end())
        return it->second;

    return -1;
}

StateSnapshot SystemSimulationSession::getSnapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return latestSnapshot_;
}

void SystemSimulationSession::addEventLog(const std::string& message, const std::string& category)
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    EventItem item;
    item.simTime = currentSimTime_.load();
    item.message = message;
    item.category = category;
    eventLog_.push_back(item);
    if (eventLog_.size() > 500)
        eventLog_.erase(eventLog_.begin(), eventLog_.begin() + 100);

    latestSnapshot_.recentEvents = eventLog_;
}

void SystemSimulationSession::applyPendingInputs(circuit_sim::TransientStepper& stepper)
{
    std::deque<InputCommand> inputs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        inputs = std::move(pendingInputs_);
        pendingInputs_.clear();
    }

    for (const auto& cmd : inputs)
    {
        const int elem = findInputElement(cmd.signalName);
        if (elem >= 0)
        {
            if (cmd.type == DataType::Numeric)
            {
                if (cmd.signalName == "speed_setpoint" || cmd.signalName.find("speed_setpoint") != std::string::npos || cmd.signalName.find("setpoint") != std::string::npos)
                {
                    // scale: 10 mV per rad/s
                    stepper.setSourceValue(elem, cmd.numericVal * 0.01);
                }
                else
                {
                    stepper.setSourceValue(elem, cmd.numericVal);
                }
            }
            else if (cmd.type == DataType::Boolean)
            {
                stepper.setSourceValue(elem, cmd.boolVal ? 5.0 : 0.0);
            }
        }
    }
}

void SystemSimulationSession::applyAllCurrentInputs(circuit_sim::TransientStepper& stepper)
{
    std::map<std::string, double> numInputs;
    std::map<std::string, bool> boolInputs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        numInputs = currentNumericInputs_;
        boolInputs = currentBooleanInputs_;
    }

    for (const auto& [sig, val] : numInputs)
    {
        const int elem = findInputElement(sig);
        if (elem >= 0)
        {
            if (sig == "speed_setpoint" || sig.find("speed_setpoint") != std::string::npos || sig.find("setpoint") != std::string::npos)
            {
                stepper.setSourceValue(elem, val * 0.01);
            }
            else
            {
                stepper.setSourceValue(elem, val);
            }
        }
    }

    for (const auto& [sig, val] : boolInputs)
    {
        const int elem = findInputElement(sig);
        if (elem >= 0)
        {
            stepper.setSourceValue(elem, val ? 5.0 : 0.0);
        }
    }

    applyPendingInputs(stepper);
}

void SystemSimulationSession::publishSnapshot(const circuit_sim::TransientStepper& stepper)
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    latestSnapshot_.simTime = stepper.time();
    latestSnapshot_.running = isRunning_.load();
    latestSnapshot_.paused = isPaused_.load();
    latestSnapshot_.faulted = isFaulted_.load();
    latestSnapshot_.targetRate = targetRate_.load();

    for (const auto& [name, node] : outputNodeBindings_)
    {
        double val = stepper.voltage(node);
        if (name == "controller.setpoint" || name.find(".setpoint") != std::string::npos
            || name.find("speed_setpoint") != std::string::npos)
        {
            latestSnapshot_.numericSignals[name] = val / 0.01; // rad/s
            if (!latestSnapshot_.numericSignals.count("speed_setpoint"))
                latestSnapshot_.numericSignals["speed_setpoint"] = val / 0.01;
            if (!latestSnapshot_.numericSignals.count("controller.setpoint"))
                latestSnapshot_.numericSignals["controller.setpoint"] = val / 0.01;
        }
        else if (name == "motor.speed" || name.find(".speed") != std::string::npos)
        {
            latestSnapshot_.numericSignals[name] = val; // rad/s
            if (!latestSnapshot_.numericSignals.count("motor.speed"))
                latestSnapshot_.numericSignals["motor.speed"] = val;
            if (!latestSnapshot_.numericSignals.count("speed"))
                latestSnapshot_.numericSignals["speed"] = val;
        }
        else if (name == "motor.angle" || name.find(".angle") != std::string::npos)
        {
            latestSnapshot_.numericSignals[name] = val; // rad
            if (!latestSnapshot_.numericSignals.count("motor.angle"))
                latestSnapshot_.numericSignals["motor.angle"] = val;
            if (!latestSnapshot_.numericSignals.count("angle"))
                latestSnapshot_.numericSignals["angle"] = val;
        }
        else if (name == "lamp.running" || name.find(".running") != std::string::npos)
        {
            const bool active = (val > 2.5);
            latestSnapshot_.booleanSignals[name] = active;
            latestSnapshot_.booleanSignals["running"] = active;
            latestSnapshot_.numericSignals[name] = val;
        }
        else if (name == "lamp.fault" || name.find(".fault") != std::string::npos)
        {
            const bool active = (val > 2.5);
            latestSnapshot_.booleanSignals[name] = active;
            latestSnapshot_.booleanSignals["fault"] = active;
            latestSnapshot_.numericSignals[name] = val;
        }
        else if (name == "system.power" || name == "power_switch")
        {
            const bool active = (val > 2.5);
            latestSnapshot_.booleanSignals[name] = active;
            latestSnapshot_.booleanSignals["power_switch"] = active;
            latestSnapshot_.numericSignals[name] = val;
        }
        else if (name == "system.ready")
        {
            latestSnapshot_.booleanSignals[name] = (val > 2.5);
            latestSnapshot_.numericSignals[name] = val;
        }
        else if (name == "system.estop" || name == "estop_button")
        {
            latestSnapshot_.booleanSignals[name] = (val > 2.5);
            latestSnapshot_.numericSignals[name] = val;
        }
        else
        {
            latestSnapshot_.numericSignals[name] = val;
        }
    }

    for (const auto& [name, elem] : outputBranchBindings_)
    {
        double val = stepper.branchCurrent(elem);
        latestSnapshot_.numericSignals[name] = val;
        if (name.find(".current") != std::string::npos)
        {
            if (!latestSnapshot_.numericSignals.count("motor.current"))
                latestSnapshot_.numericSignals["motor.current"] = val;
        }
    }
}

void SystemSimulationSession::workerThreadLoop()
{
    circuit_sim::TransientStepper stepper(baseCircuit_);
    if (!stepper.init())
    {
        isFaulted_ = true;
        addEventLog("Stepper initialization failed: " + stepper.error(), "FAULT");
        isRunning_ = false;
        return;
    }

    // Apply all currently configured operator controls immediately to new stepper
    applyAllCurrentInputs(stepper);

    publishSnapshot(stepper);

    auto lastWallTime = std::chrono::steady_clock::now();
    double simClock = 0.0;
    const double stepChunk = 0.005; // 5 ms simulation chunk per pacing round

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (shouldExit_)
                break;

            if (isPaused_ && !singleStepRequested_)
            {
                cv_.wait(lock, [this] { return shouldExit_ || !isPaused_ || singleStepRequested_; });
                if (shouldExit_)
                    break;
                lastWallTime = std::chrono::steady_clock::now();
            }
        }

        applyPendingInputs(stepper);

        bool doStep = false;
        double advanceDt = stepChunk;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (singleStepRequested_)
            {
                advanceDt = singleStepDt_;
                singleStepRequested_ = false;
                doStep = true;
            }
        }

        if (!isPaused_ || doStep)
        {
            simClock += advanceDt;
            if (!stepper.advance(simClock))
            {
                isFaulted_ = true;
                addEventLog("Simulation step failed: " + stepper.error(), "FAULT");
                break;
            }

            currentSimTime_ = stepper.time();
            publishSnapshot(stepper);
        }

        // Pacing with wall clock
        const double rate = targetRate_.load();
        if (rate > 0.0 && !isPaused_)
        {
            const auto targetWallInterval = std::chrono::duration<double>(stepChunk / rate);
            const auto nextWallDeadline = lastWallTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(targetWallInterval);
            const auto now = std::chrono::steady_clock::now();

            if (nextWallDeadline > now)
            {
                std::this_thread::sleep_for(nextWallDeadline - now);
            }
            lastWallTime = std::chrono::steady_clock::now();
        }
        else if (isPaused_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    isRunning_ = false;
}

} // namespace sim_panel

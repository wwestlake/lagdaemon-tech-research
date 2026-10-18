#pragma once

#include "SystemSimulationSession.h"
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



namespace dcad_motion
{

// -----------------------------------------------------------------------------
// Constants & Framing
// -----------------------------------------------------------------------------
constexpr uint32_t kMagicNumber = 0xF2B49E2C;
constexpr int64_t kTicksPerSecond = 1000000000000LL; // 1 ps ticks (10^12)
constexpr const char* kLifecycleCapability = "lifecycleControl.v1";

// -----------------------------------------------------------------------------
// Protocol Data Types
// -----------------------------------------------------------------------------
struct EndpointInfo
{
    std::string protocol;
    int protocolVersion = 1;
    std::string pipeName;
    std::string pipePath;
    uint32_t processId = 0;
    std::string modelId;
    std::string documentName;
    std::string startedUtc;
};

struct SignalDescriptor
{
    std::string id;
    std::string name;
    std::string quantity; // "angle", "angular_velocity", "position", "linear_velocity"
    std::string unit;     // "rad", "deg", "rev", "rad/s", "deg/s", "rpm", "rev/s", "m", "mm", "m/s", "mm/s"
    std::string kind;     // "state", "rate"

    juce::var toJson() const;
    static SignalDescriptor fromJson(const juce::var& json);
};

struct ProfileMapping
{
    std::string joint;
    std::string signal;
    std::string sourceQuantity;
    std::string sourceUnit;
    std::string jointQuantity;
    std::string jointUnit;
    double unitToSI = 1.0;
    double scale = 1.0;
    double offset = 0.0;
    bool hasInitialCoordinate = false;
    double initialCoordinate = 0.0;

    static ProfileMapping fromJson(const juce::var& json);
};

struct ValidationError
{
    std::string code;
    std::string joint;
    std::string signal;
    std::string message;

    juce::var toJson() const;
};

enum class LinkState
{
    Disconnected,
    Connecting,
    Handshaking,
    Advertising,
    Binding,
    Synchronizing,
    Streaming,
    Error
};

// -----------------------------------------------------------------------------
// Unit Conversions
// -----------------------------------------------------------------------------
double getUnitToSIFactor(const std::string& quantity, const std::string& unit);
bool isRateQuantity(const std::string& quantity);

// -----------------------------------------------------------------------------
// DcadMotionClient
// -----------------------------------------------------------------------------
class DcadMotionClient
{
public:
    DcadMotionClient();
    ~DcadMotionClient();

    DcadMotionClient(const DcadMotionClient&) = delete;
    DcadMotionClient& operator=(const DcadMotionClient&) = delete;

    // Attach simulation session
    void attachSession(std::shared_ptr<sim_panel::SystemSimulationSession> session);
    std::shared_ptr<sim_panel::SystemSimulationSession> getSession() const { return session_; }

    // Set simulation/session identity
    void setSimulationIdentity(const std::string& simId, const std::string& simName = "DEWB Simulation");
    std::string getSimulationId() const { return simulationId_; }

    // Authoritative signal catalog configuration
    void setSignalCatalog(const std::vector<SignalDescriptor>& signals);
    std::vector<SignalDescriptor> getSignalCatalog() const;
    void refreshCatalogFromActiveSession();

    // Discovery
    static std::vector<EndpointInfo> discoverEndpoints();

    // Connection lifecycle
    bool connectToPipe(const std::string& pipePath);
    void disconnect(bool sendGoodbye = true);
    bool isConnected() const;

    // Current status snapshot
    struct StatusSnapshot
    {
        LinkState state = LinkState::Disconnected;
        std::string pipePath;
        std::string modelId;
        std::string documentName;
        int interfaceRevision = 0;
        int profileRevision = 0;
        std::string simulationId;
        int timelineGeneration = 0;
        uint64_t lastSequence = 0;
        int64_t simTimePs = 0;
        std::string lifecycle = "stopped";
        size_t mappingCount = 0;
        std::string lastError;
        bool dcadLifecycleControl = false;
    };

    StatusSnapshot getStatus() const;

    // Lifecycle control methods (can be called from UI/Agent or triggered via DCAD)
    struct LifecycleExecutionResult
    {
        std::string requestId;
        std::string command;
        std::string outcome; // "applied", "rejected", "accepted"
        std::string lifecycle;
        int timelineGeneration = 0;
        int64_t simTimePs = 0;
        uint64_t stateSequence = 0;
        std::string code;
        std::string message;

        juce::var toJson(const std::string& simulationId, int profileRevision) const;
    };

    LifecycleExecutionResult executeLifecycleCommand(const std::string& command,
                                                    int64_t stepDurationPs = 0,
                                                    const std::string& requestId = "");

    // Direct solver frame injection / polling
    void publishCurrentStateFrame();

    // Callbacks for UI updates
    std::function<void(const StatusSnapshot&)> onStatusChanged;
    std::function<void(const std::string&)> onLogMessage;

private:
    void threadLoop();
    void sendFrame(const juce::var& jsonPayload);
    bool readIncomingFrames(std::vector<juce::var>& outFrames);
    void handleIncomingMessage(const juce::var& msg);

    // Protocol message handlers
    void handleHello(const juce::var& msg);
    void handleMechanicalInterface(const juce::var& msg);
    void handleBindingProposal(const juce::var& msg);
    void handleStateApplied(const juce::var& msg);
    void handleLifecycleCommand(const juce::var& msg);
    void handleLinkStatus(const juce::var& msg);

    void sendHello();
    void sendSignalInterface();
    void sendBindingAccept();
    void sendBindingReject(const std::vector<ValidationError>& errors);
    void sendSynchronizeState(const std::string& lifecycle, bool newTimeline = false);
    void sendStateFrame(const std::string& lifecycle);
    void sendHeartbeat();
    void sendGoodbye();
    void sendLifecycleResult(const LifecycleExecutionResult& res);

    std::vector<ValidationError> validateProposal(const std::vector<ProfileMapping>& mappings);

    struct EvaluatedJoint
    {
        std::string joint;
        double coordinate = 0.0;
        bool hasVelocity = false;
        double velocity = 0.0;

        juce::var toJson() const;
    };

    std::vector<EvaluatedJoint> computeJointCoordinates(double simTimeSeconds, double dtSeconds);

    // Logging helper
    void log(const std::string& message);

    // Native Windows Named Pipe Handle (void* to keep windows.h out of public header)
    void* pipeHandle_ = nullptr;

    std::shared_ptr<sim_panel::SystemSimulationSession> session_;
    std::string simulationId_ = "dewb-session-1";
    std::string simulationName_ = "DEWB System Simulator";
    int signalInterfaceRevision_ = 1;

    std::vector<SignalDescriptor> signals_;

    // Negotiated connection state
    mutable std::recursive_mutex mutex_;
    LinkState state_ = LinkState::Disconnected;
    std::string pipePath_;
    std::string modelId_;
    std::string documentName_;
    int interfaceRevision_ = 0;
    int profileRevision_ = 0;
    bool dcadLifecycleControl_ = false;

    std::vector<ProfileMapping> acceptedMappings_;
    std::map<std::string, double> rateIntegrals_; // joint -> integrated coordinate

    int timelineGeneration_ = 0;
    uint64_t sequence_ = 0;
    int64_t currentSimTimePs_ = 0;
    std::string currentLifecycle_ = "stopped";
    bool initial_ = true;

    // Deduplication table: requestId -> LifecycleExecutionResult
    std::map<std::string, LifecycleExecutionResult> executedResults_;

    // Threading & I/O
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> shouldExit_ { false };
    std::condition_variable_any cv_;
    double lastHeartbeatTime_ = 0.0;
    std::string lastErrorMessage_;

    // Frame receive buffer
    std::vector<uint8_t> readBuffer_;

    // SynchronizeState acknowledgement tracking
    std::atomic<bool> awaitingStateApplied_ { false };
    std::string awaitingSyncSequence_;
};

} // namespace dcad_motion

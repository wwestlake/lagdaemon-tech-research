#include "DcadMotionClient.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace dcad_motion
{

// -----------------------------------------------------------------------------
// Serialization & Deserialization
// -----------------------------------------------------------------------------
juce::var SignalDescriptor::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(id));
    if (!name.empty())
        obj->setProperty("name", juce::String(name));
    obj->setProperty("quantity", juce::String(quantity));
    obj->setProperty("unit", juce::String(unit));
    obj->setProperty("kind", juce::String(kind));
    return juce::var(obj);
}

SignalDescriptor SignalDescriptor::fromJson(const juce::var& json)
{
    SignalDescriptor desc;
    if (auto* obj = json.getDynamicObject())
    {
        desc.id = obj->getProperty("id").toString().toStdString();
        desc.name = obj->getProperty("name").toString().toStdString();
        desc.quantity = obj->getProperty("quantity").toString().toStdString();
        desc.unit = obj->getProperty("unit").toString().toStdString();
        desc.kind = obj->getProperty("kind").toString().toStdString();
    }
    return desc;
}

juce::var DcadMotionClient::LifecycleExecutionResult::toJson(const std::string& simId, int profRev) const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "LifecycleResult");
    obj->setProperty("requestId", juce::String(requestId));
    obj->setProperty("simulationId", juce::String(simId));
    obj->setProperty("profileRevision", profRev);
    obj->setProperty("command", juce::String(command));
    obj->setProperty("outcome", juce::String(outcome));
    obj->setProperty("lifecycle", juce::String(lifecycle));
    obj->setProperty("timelineGeneration", timelineGeneration);
    obj->setProperty("simTimePs", juce::String(std::to_string(simTimePs)));
    if (stateSequence > 0)
        obj->setProperty("stateSequence", juce::String(std::to_string(stateSequence)));
    if (!code.empty())
    {
        obj->setProperty("code", juce::String(code));
        obj->setProperty("message", juce::String(message));
    }
    return juce::var(obj);
}

// -----------------------------------------------------------------------------
// DcadMotionClient Implementation
// -----------------------------------------------------------------------------
DcadMotionClient::DcadMotionClient()
{
#if JUCE_WINDOWS
    pipeHandle_ = INVALID_HANDLE_VALUE;
#endif
    // Generate a default simulation ID
    simulationId_ = "dewb-session-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() & 0xFFFF);
    refreshCatalogFromActiveSession();
}

DcadMotionClient::~DcadMotionClient()
{
    disconnect(true);
}

void DcadMotionClient::attachSession(std::shared_ptr<sim_panel::SystemSimulationSession> session)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    session_ = session;
    refreshCatalogFromActiveSession();
}

void DcadMotionClient::setSimulationIdentity(const std::string& simId, const std::string& simName)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    simulationId_ = simId;
    simulationName_ = simName;
}

void DcadMotionClient::setSignalCatalog(const std::vector<SignalDescriptor>& signals)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    signals_ = signals;
}

std::vector<SignalDescriptor> DcadMotionClient::getSignalCatalog() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return signals_;
}

void DcadMotionClient::refreshCatalogFromActiveSession()
{
    std::vector<SignalDescriptor> cat;

    if (session_)
    {
        const auto& defs = session_->signalDefinitions();
        for (const auto& d : defs)
        {
            if (!d.isOutput)
                continue;

            // Only mechanical motion quantities are relevant for CAD joint motion
            if (d.quantity == "angular_velocity" || d.quantity == "angle" ||
                d.quantity == "linear_velocity"  || d.quantity == "position")
            {
                SignalDescriptor desc;
                desc.id = d.name;
                desc.name = d.description.empty() ? d.name : d.description;
                desc.quantity = d.quantity;
                desc.unit = d.unit;
                desc.kind = d.kind.empty() ? (isRateQuantity(d.quantity) ? "rate" : "state") : d.kind;
                cat.push_back(desc);
            }
        }
    }

    signals_ = std::move(cat);
}

std::vector<EndpointInfo> DcadMotionClient::discoverEndpoints()
{
    std::vector<EndpointInfo> results;

    juce::File motionDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("Local")
                               .getChildFile("DjehutiCAD")
                               .getChildFile("MotionEndpoints");

    if (!motionDir.isDirectory())
    {
        // On Windows sometimes userApplicationDataDirectory points to AppData\Roaming, check AppData\Local directly
        auto localAppData = juce::File(juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {}));
        if (localAppData.isDirectory())
            motionDir = localAppData.getChildFile("DjehutiCAD").getChildFile("MotionEndpoints");
    }

    if (motionDir.isDirectory())
    {
        auto files = motionDir.findChildFiles(juce::File::findFiles, false, "*.json");
        for (const auto& file : files)
        {
            auto json = juce::JSON::parse(file.loadFileAsString());
            if (auto* obj = json.getDynamicObject())
            {
                EndpointInfo info;
                info.protocol = obj->getProperty("protocol").toString().toStdString();
                info.protocolVersion = (int)obj->getProperty("protocolVersion");
                info.pipeName = obj->getProperty("pipeName").toString().toStdString();
                info.pipePath = obj->getProperty("pipePath").toString().toStdString();
                info.processId = (uint32_t)(int)obj->getProperty("processId");
                info.modelId = obj->getProperty("modelId").toString().toStdString();
                info.documentName = obj->getProperty("documentName").toString().toStdString();
                info.startedUtc = obj->getProperty("startedUtc").toString().toStdString();

                if (info.protocol == "djehuti.motion" && info.protocolVersion == 1)
                    results.push_back(info);
            }
        }
    }

    return results;
}

bool DcadMotionClient::connectToPipe(const std::string& pipePath)
{
    disconnect(false);

    log("Connecting to named pipe: " + pipePath);

#if JUCE_WINDOWS
    juce::String widePath(pipePath);
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (std::chrono::steady_clock::now() < deadline)
    {
        hPipe = CreateFileW(
            (LPCWSTR)widePath.toWideCharPointer(),
            GENERIC_READ | GENERIC_WRITE,
            0,              // no sharing
            NULL,           // default security
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeW((LPCWSTR)widePath.toWideCharPointer(), 100);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        log("Failed to open pipe handle (Error " + std::to_string(err) + ")");
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = LinkState::Error;
        lastErrorMessage_ = "CreateFileW failed (Win32 error " + std::to_string(err) + ")";
        return false;
    }

    // Set pipe to message read mode
    DWORD dwMode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(hPipe, &dwMode, NULL, NULL);

    pipeHandle_ = hPipe;
#endif

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        pipePath_ = pipePath;
        state_ = LinkState::Connecting;
        shouldExit_ = false;
        readBuffer_.clear();
        acceptedMappings_.clear();
        executedResults_.clear();
        awaitingStateApplied_ = false;
    }

    // Start background link worker thread
    thread_ = std::make_unique<std::thread>(&DcadMotionClient::threadLoop, this);

    // Send Hello immediately
    sendHello();

    return true;
}

void DcadMotionClient::disconnect(bool shouldSendGoodbye)
{
    if (shouldSendGoodbye && isConnected())
    {
        sendGoodbye();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    shouldExit_ = true;
    cv_.notify_all();

    if (thread_ && thread_->joinable())
    {
        thread_->join();
        thread_.reset();
    }

#if JUCE_WINDOWS
    if (pipeHandle_ != INVALID_HANDLE_VALUE && pipeHandle_ != nullptr)
    {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }
#endif

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = LinkState::Disconnected;
        acceptedMappings_.clear();
        awaitingStateApplied_ = false;
    }

    log("Disconnected from DCAD");
    if (onStatusChanged)
        onStatusChanged(getStatus());
}

bool DcadMotionClient::isConnected() const
{
#if JUCE_WINDOWS
    return pipeHandle_ != INVALID_HANDLE_VALUE && pipeHandle_ != nullptr;
#else
    return false;
#endif
}

DcadMotionClient::StatusSnapshot DcadMotionClient::getStatus() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    StatusSnapshot s;
    s.state = state_;
    s.pipePath = pipePath_;
    s.modelId = modelId_;
    s.documentName = documentName_;
    s.interfaceRevision = interfaceRevision_;
    s.profileRevision = profileRevision_;
    s.simulationId = simulationId_;
    s.timelineGeneration = timelineGeneration_;
    s.lastSequence = sequence_;
    s.simTimePs = currentSimTimePs_;
    s.lifecycle = currentLifecycle_;
    s.mappingCount = acceptedMappings_.size();
    s.lastError = lastErrorMessage_;
    s.dcadLifecycleControl = dcadLifecycleControl_;
    return s;
}

void DcadMotionClient::log(const std::string& message)
{
    if (onLogMessage)
        onLogMessage(message);
}

// -----------------------------------------------------------------------------
// Low-Level Transport Framing
// -----------------------------------------------------------------------------
void DcadMotionClient::sendFrame(const juce::var& jsonPayload)
{
#if JUCE_WINDOWS
    if (pipeHandle_ == INVALID_HANDLE_VALUE || pipeHandle_ == nullptr)
        return;

    const auto jsonString = juce::JSON::toString(jsonPayload, true);
    juce::MemoryOutputStream utf8;
    utf8.writeString(jsonString);

    const uint32_t payloadLen = (uint32_t)utf8.getDataSize();
    const uint32_t magic = kMagicNumber;

    juce::MemoryOutputStream packet;
    packet.writeInt(magic);
    packet.writeInt(payloadLen);
    packet.write(utf8.getData(), payloadLen);

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(pipeHandle_, packet.getData(), (DWORD)packet.getDataSize(), &bytesWritten, NULL);
    if (!ok)
    {
        DWORD err = GetLastError();
        log("WriteFile error " + std::to_string(err) + " - pipe closed or stalled");
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = LinkState::Error;
        lastErrorMessage_ = "WriteFile failed (" + std::to_string(err) + ")";
    }
#endif
}

bool DcadMotionClient::readIncomingFrames(std::vector<juce::var>& outFrames)
{
#if JUCE_WINDOWS
    if (pipeHandle_ == INVALID_HANDLE_VALUE || pipeHandle_ == nullptr)
        return false;

    DWORD bytesAvail = 0;
    if (!PeekNamedPipe(pipeHandle_, NULL, 0, NULL, &bytesAvail, NULL))
    {
        DWORD err = GetLastError();
        log("PeekNamedPipe error " + std::to_string(err) + " - connection lost");
        return false;
    }

    if (bytesAvail > 0)
    {
        std::vector<uint8_t> temp(bytesAvail);
        DWORD bytesRead = 0;
        if (ReadFile(pipeHandle_, temp.data(), bytesAvail, &bytesRead, NULL) && bytesRead > 0)
        {
            readBuffer_.insert(readBuffer_.end(), temp.begin(), temp.begin() + bytesRead);
        }
    }

    // Unpack frames
    while (readBuffer_.size() >= 8)
    {
        uint32_t magic = (uint32_t)readBuffer_[0] |
                         ((uint32_t)readBuffer_[1] << 8) |
                         ((uint32_t)readBuffer_[2] << 16) |
                         ((uint32_t)readBuffer_[3] << 24);

        if (magic != kMagicNumber)
        {
            log("Corrupt frame magic 0x" + juce::String::toHexString((int)magic).toStdString() + ", clearing buffer");
            readBuffer_.clear();
            return false;
        }

        uint32_t length = (uint32_t)readBuffer_[4] |
                          ((uint32_t)readBuffer_[5] << 8) |
                          ((uint32_t)readBuffer_[6] << 16) |
                          ((uint32_t)readBuffer_[7] << 24);

        if (length > 1024 * 1024)
        {
            log("Frame length exceeds 1 MiB limit, closing");
            readBuffer_.clear();
            return false;
        }

        if (readBuffer_.size() < 8 + length)
            break; // need more data

        juce::String jsonStr = juce::String::fromUTF8((const char*)readBuffer_.data() + 8, (int)length);
        readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + 8 + length);

        auto parsed = juce::JSON::parse(jsonStr);
        if (parsed.isObject())
            outFrames.push_back(parsed);
        else
            log("Malformed incoming JSON object");
    }

    return true;
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
// Background Link Thread Loop
// -----------------------------------------------------------------------------
void DcadMotionClient::threadLoop()
{
    log("DcadMotionClient worker thread running");

    lastHeartbeatTime_ = juce::Time::getMillisecondCounterHiRes() * 0.001;

    double lastFrameTime = lastHeartbeatTime_;
    double lastSimTime = 0.0;
    if (session_)
        lastSimTime = session_->simTime();

    while (!shouldExit_)
    {
        // 1. Process incoming messages
        std::vector<juce::var> incoming;
        if (!readIncomingFrames(incoming))
        {
            // Error on pipe read
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            state_ = LinkState::Error;
            lastErrorMessage_ = "Pipe connection dropped";
            break;
        }

        for (const auto& msg : incoming)
        {
            handleIncomingMessage(msg);
        }

        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        // 2. State Streaming: if Bound & Synchronized and not awaiting StateApplied
        bool canStream = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            canStream = (state_ == LinkState::Streaming && !awaitingStateApplied_);
        }

        if (canStream && session_)
        {
            const auto snap = session_->getSnapshot();
            std::string snapLifecycle = "stopped";
            if (snap.faulted) snapLifecycle = "faulted";
            else if (snap.running) snapLifecycle = snap.paused ? "paused" : "running";
            else snapLifecycle = "stopped";

            // If running, stream at ~30 Hz (dt >= 0.033s)
            if (snapLifecycle == "running")
            {
                if (now - lastFrameTime >= 0.033)
                {
                    sendStateFrame("running");
                    lastFrameTime = now;
                    lastHeartbeatTime_ = now;
                }
            }
            else
            {
                // If lifecycle changed while stopped/paused/faulted, publish immediately
                if (snapLifecycle != currentLifecycle_)
                {
                    sendStateFrame(snapLifecycle);
                    lastHeartbeatTime_ = now;
                }
            }
        }

        // 3. Heartbeat keeping: send at least every 800ms
        if (now - lastHeartbeatTime_ >= 0.8)
        {
            sendHeartbeat();
            lastHeartbeatTime_ = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    log("DcadMotionClient worker thread finished");
}

// -----------------------------------------------------------------------------
// Inbound Message Dispatch
// -----------------------------------------------------------------------------
void DcadMotionClient::handleIncomingMessage(const juce::var& msg)
{
    if (!msg.isObject()) return;
    const std::string type = msg.getProperty("type", {}).toString().toStdString();

    if (type == "Hello")
    {
        handleHello(msg);
    }
    else if (type == "MechanicalInterface")
    {
        handleMechanicalInterface(msg);
    }
    else if (type == "BindingProposal")
    {
        handleBindingProposal(msg);
    }
    else if (type == "StateApplied")
    {
        handleStateApplied(msg);
    }
    else if (type == "LifecycleCommand")
    {
        handleLifecycleCommand(msg);
    }
    else if (type == "LinkStatus")
    {
        handleLinkStatus(msg);
    }
    else
    {
        log("Received message type: " + type);
    }
}

void DcadMotionClient::handleHello(const juce::var& msg)
{
    log("DCAD -> Hello received");
    if (const auto* caps = msg.getProperty("capabilities", {}).getArray())
    {
        for (const auto& c : *caps)
        {
            if (c.toString().toStdString() == kLifecycleCapability)
                dcadLifecycleControl_ = true;
        }
    }
}

void DcadMotionClient::handleMechanicalInterface(const juce::var& msg)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    modelId_ = msg.getProperty("modelId", {}).toString().toStdString();
    documentName_ = msg.getProperty("documentName", {}).toString().toStdString();
    interfaceRevision_ = (int)msg.getProperty("interfaceRevision", 0);
    profileRevision_ = (int)msg.getProperty("profileRevision", 0);

    log("DCAD -> MechanicalInterface: model " + modelId_ + " (" + documentName_ + "), rev " + std::to_string(interfaceRevision_));

    state_ = LinkState::Advertising;
    sendSignalInterface();
}

void DcadMotionClient::handleBindingProposal(const juce::var& msg)
{
    log("DCAD -> BindingProposal received");
    std::vector<ProfileMapping> proposed;

    if (const auto* arr = msg.getProperty("mappings", {}).getArray())
    {
        for (const auto& item : *arr)
            proposed.push_back(ProfileMapping::fromJson(item));
    }

    const int propRevision = (int)msg.getProperty("profileRevision", 0);
    auto problems = validateProposal(proposed);
    if (!problems.empty())
    {
        log("Validation rejected " + std::to_string(problems.size()) + " proposal mappings");
        sendBindingReject(propRevision, problems);
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = LinkState::Advertising;
        lastErrorMessage_ = "Binding proposal rejected with validation errors";
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        modelId_ = msg.getProperty("modelId", {}).toString().toStdString();
        interfaceRevision_ = (int)msg.getProperty("interfaceRevision", 0);
        profileRevision_ = (int)msg.getProperty("profileRevision", 0);
        acceptedMappings_ = std::move(proposed);

        // Initialize rate coordinates only for new joints that are not already tracked
        for (const auto& m : acceptedMappings_)
        {
            if (rateIntegrals_.find(m.joint) == rateIntegrals_.end())
            {
                if (m.hasInitialCoordinate)
                    rateIntegrals_[m.joint] = m.initialCoordinate;
                else
                    rateIntegrals_[m.joint] = 0.0;
            }
        }

        state_ = LinkState::Binding;
    }

    sendBindingAccept();

    // After BindingAccept, send SynchronizeState. Only start a new timeline generation on first binding.
    const bool isFirstTimeline = (timelineGeneration_ == 0);
    sendSynchronizeState(currentLifecycle_, isFirstTimeline);
}

void DcadMotionClient::handleStateApplied(const juce::var& msg)
{
    const std::string seq = msg.getProperty("sequence", {}).toString().toStdString();
    const bool isDuplicate = (bool)msg.getProperty("duplicate", false);

    log("DCAD -> StateApplied: sequence " + seq + (isDuplicate ? " (duplicate)" : ""));

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (awaitingStateApplied_ && seq == awaitingSyncSequence_)
    {
        awaitingStateApplied_ = false;
        state_ = LinkState::Streaming;
        log("Timeline synchronization confirmed by DCAD, now streaming.");
    }

    if (onStatusChanged)
        onStatusChanged(getStatus());
}

void DcadMotionClient::handleLinkStatus(const juce::var& msg)
{
    const std::string code = msg.getProperty("code", {}).toString().toStdString();
    if (code == "heartbeat")
        return;

    const std::string sev = msg.getProperty("severity", {}).toString().toStdString();
    const std::string message = msg.getProperty("message", {}).toString().toStdString();
    log("DCAD LinkStatus [" + sev + "/" + code + "]: " + message);

    if (code == "revalidationRequired")
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        state_ = LinkState::Advertising;
        awaitingStateApplied_ = false;
        log("Revalidation requested by DCAD, awaiting fresh proposal.");
    }
}

// -----------------------------------------------------------------------------
// Lifecycle Commands & Deduplication
// -----------------------------------------------------------------------------
void DcadMotionClient::handleLifecycleCommand(const juce::var& msg)
{
    const std::string reqId = msg.getProperty("requestId", {}).toString().toStdString();
    const std::string cmd = msg.getProperty("command", {}).toString().toStdString();
    const std::string simId = msg.getProperty("simulationId", {}).toString().toStdString();
    const std::string modId = msg.getProperty("modelId", {}).toString().toStdString();
    const int profRev = (int)msg.getProperty("profileRevision", 0);
    const int reqTimeline = (int)msg.getProperty("timelineGeneration", 0);

    int64_t stepDurPs = 0;
    auto stepDurVar = msg.getProperty("stepDurationPs", {});
    if (!stepDurVar.isVoid())
        stepDurPs = std::stoll(stepDurVar.toString().toStdString());

    log("DCAD -> LifecycleCommand " + cmd + " (id: " + reqId + ")");

    // Deduplication check: if requestId already handled, return stored result without executing again
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = executedResults_.find(reqId);
        if (it != executedResults_.end())
        {
            log("Deduplicating LifecycleCommand " + cmd + " (" + reqId + "): returning cached result");
            sendLifecycleResult(it->second);
            return;
        }
    }

    // Verify binding identity
    if (simId != simulationId_ || modId != modelId_ || profRev != profileRevision_)
    {
        LifecycleExecutionResult res;
        res.requestId = reqId;
        res.command = cmd;
        res.outcome = "rejected";
        res.code = "staleBinding";
        res.message = "The command was issued for a different binding.";
        res.lifecycle = currentLifecycle_;
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        sendLifecycleResult(res);
        return;
    }

    if (reqTimeline != timelineGeneration_)
    {
        LifecycleExecutionResult res;
        res.requestId = reqId;
        res.command = cmd;
        res.outcome = "rejected";
        res.code = "staleTimeline";
        res.message = "The command refers to timeline " + std::to_string(reqTimeline) + "; current is " + std::to_string(timelineGeneration_);
        res.lifecycle = currentLifecycle_;
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        sendLifecycleResult(res);
        return;
    }

    // Execute the lifecycle transition
    LifecycleExecutionResult result = executeLifecycleCommand(cmd, stepDurPs, reqId);
    sendLifecycleResult(result);
}

DcadMotionClient::LifecycleExecutionResult DcadMotionClient::executeLifecycleCommand(
    const std::string& command,
    int64_t stepDurationPs,
    const std::string& requestId)
{
    LifecycleExecutionResult res;
    res.requestId = requestId.empty() ? ("req-" + std::to_string(juce::Time::getMillisecondCounter())) : requestId;
    res.command = command;

    // Deduplication check: if requestId already executed, return cached result immediately
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = executedResults_.find(res.requestId);
        if (it != executedResults_.end())
        {
            return it->second;
        }
    }

    if (!session_)
    {
        res.outcome = "rejected";
        res.code = "faulted";
        res.message = "No active simulation session attached.";
        res.lifecycle = "stopped";
        return res;
    }

    const auto snap = session_->getSnapshot();
    std::string state = "stopped";
    if (snap.faulted) state = "faulted";
    else if (snap.running) state = snap.paused ? "paused" : "running";
    else state = "stopped";

    if (command == "start")
    {
        if (state != "stopped")
        {
            res.outcome = "rejected";
            res.code = "invalidTransition";
            res.message = "Start needs a stopped simulation; it is " + state + ".";
            res.lifecycle = state;
            res.timelineGeneration = timelineGeneration_;
            res.simTimePs = currentSimTimePs_;
            return res;
        }

        if (!initial_)
        {
            // Fresh start: increment timeline and synchronize first
            sendSynchronizeState("running", true);
        }

        session_->start();
        initial_ = false;
        currentLifecycle_ = "running";
        sendStateFrame("running");

        res.outcome = "applied";
        res.lifecycle = "running";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        res.stateSequence = sequence_;
    }
    else if (command == "pause")
    {
        if (state != "running")
        {
            res.outcome = "rejected";
            res.code = "invalidTransition";
            res.message = "Pause needs a running simulation; it is " + state + ".";
            res.lifecycle = state;
            res.timelineGeneration = timelineGeneration_;
            res.simTimePs = currentSimTimePs_;
            return res;
        }

        session_->pause();
        currentLifecycle_ = "paused";
        sendStateFrame("paused");

        res.outcome = "applied";
        res.lifecycle = "paused";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        res.stateSequence = sequence_;
    }
    else if (command == "resume")
    {
        if (state != "paused")
        {
            res.outcome = "rejected";
            res.code = "invalidTransition";
            res.message = "Resume needs a paused simulation; it is " + state + ".";
            res.lifecycle = state;
            res.timelineGeneration = timelineGeneration_;
            res.simTimePs = currentSimTimePs_;
            return res;
        }

        session_->resume();
        currentLifecycle_ = "running";
        sendStateFrame("running");

        res.outcome = "applied";
        res.lifecycle = "running";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        res.stateSequence = sequence_;
    }
    else if (command == "stop")
    {
        if (state != "running" && state != "paused")
        {
            res.outcome = "rejected";
            res.code = "invalidTransition";
            res.message = "Stop needs a running or paused simulation; it is " + state + ".";
            res.lifecycle = state;
            res.timelineGeneration = timelineGeneration_;
            res.simTimePs = currentSimTimePs_;
            return res;
        }

        session_->stop();
        currentLifecycle_ = "stopped";
        sendStateFrame("stopped");

        res.outcome = "applied";
        res.lifecycle = "stopped";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        res.stateSequence = sequence_;
    }
    else if (command == "reset")
    {
        session_->reset();
        currentLifecycle_ = "stopped";
        initial_ = true;

        // Reset: new timeline, full synchronization at t=0
        sendSynchronizeState("stopped", true);

        res.outcome = "applied";
        res.lifecycle = "stopped";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = 0;
        res.stateSequence = sequence_;
    }
    else if (command == "step")
    {
        if (!(state == "paused" || (state == "stopped" && initial_)))
        {
            res.outcome = "rejected";
            res.code = "invalidTransition";
            res.message = "Single Step needs a paused simulation (or initial state); it is " + state + ".";
            res.lifecycle = state;
            res.timelineGeneration = timelineGeneration_;
            res.simTimePs = currentSimTimePs_;
            return res;
        }

        double dtSeconds = 0.001; // default 1 ms
        if (stepDurationPs > 0)
            dtSeconds = (double)stepDurationPs / (double)kTicksPerSecond;

        session_->stepSingle(dtSeconds);
        currentLifecycle_ = "paused";
        sendStateFrame("paused");

        res.outcome = "applied";
        res.lifecycle = "paused";
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
        res.stateSequence = sequence_;
    }
    else
    {
        res.outcome = "rejected";
        res.code = "unknownCommand";
        res.message = "Unrecognized lifecycle command: " + command;
        res.lifecycle = state;
        res.timelineGeneration = timelineGeneration_;
        res.simTimePs = currentSimTimePs_;
    }

    // Store in deduplication table
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        executedResults_[res.requestId] = res;
    }

    return res;
}

// -----------------------------------------------------------------------------
// Validation Logic
// -----------------------------------------------------------------------------
std::vector<ValidationError> DcadMotionClient::validateProposal(const std::vector<ProfileMapping>& mappings)
{
    std::map<std::string, MotionSignalDef> catalog;
    for (const auto& s : signals_)
    {
        MotionSignalDef def;
        def.id = s.id;
        def.name = s.name;
        def.quantity = s.quantity;
        def.unit = s.unit;
        def.kind = s.kind;
        def.companionSignal = findDeclaredCompanionSignal(s.id);
        catalog[s.id] = def;
    }

    const bool atInitialTime = (currentSimTimePs_ == 0 && (!session_ || session_->simTime() <= 1e-9));

    auto hasHist = [this](const std::string& rateSig) -> bool {
        if (!session_) return false;
        auto snap = session_->getSnapshot();
        return (snap.rateIntegrals.find(rateSig) != snap.rateIntegrals.end());
    };

    return validateProposalMappings(catalog, mappings, atInitialTime, hasHist);
}

std::string DcadMotionClient::findDeclaredCompanionSignal(const std::string& rateSignal) const
{
    if (!session_)
        return {};

    const auto& defs = session_->signalDefinitions();
    for (const auto& d : defs)
    {
        if (d.name == rateSignal && !d.companionSignal.empty())
        {
            return d.companionSignal;
        }
    }
    return {};
}

// -----------------------------------------------------------------------------
// Joint Evaluation & Mathematical Absolute State
// -----------------------------------------------------------------------------
bool DcadMotionClient::computeJointCoordinates(std::vector<EvaluatedJoint>& evaluated,
                                             const sim_panel::StateSnapshot& snap,
                                             std::map<std::string, double>& outRateIntegrals,
                                             std::map<std::string, double>& outCompanionOrigins)
{
    auto findCompanionFunc = [this](const std::string& sig) {
        return findDeclaredCompanionSignal(sig);
    };

    auto getCompUnitToSI = [this](const std::string& comp) {
        if (session_)
        {
            for (const auto& d : session_->signalDefinitions())
            {
                if (d.name == comp)
                    return getUnitToSIFactor(d.quantity, d.unit);
            }
        }
        return 1.0;
    };

    bool ok = evaluateJointCoordinates(
        acceptedMappings_,
        snap.numericSignals,
        snap.rateIntegrals,
        findCompanionFunc,
        getCompUnitToSI,
        outRateIntegrals,
        outCompanionOrigins,
        evaluated,
        lastErrorMessage_
    );

    if (!ok)
    {
        log("Error: " + lastErrorMessage_);
    }

    return ok;
}

// -----------------------------------------------------------------------------
// Protocol Outbound Messages
// -----------------------------------------------------------------------------
void DcadMotionClient::sendHello()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "Hello");
    obj->setProperty("protocol", "djehuti.motion");
    obj->setProperty("protocolVersion", 1);
    obj->setProperty("role", "dewb");
    obj->setProperty("application", "Djehuti Electronics Workbench");
    obj->setProperty("applicationVersion", "1.0.0");

    juce::Array<juce::var> caps;
    caps.add("signalInterface.v1");
    caps.add("synchronizeState.v1");
    caps.add("stateFrame.v1");
    caps.add("heartbeat.v1");
    caps.add(kLifecycleCapability);
    obj->setProperty("capabilities", caps);

    sendFrame(juce::var(obj));
    log("DEWB -> Hello sent");
}

void DcadMotionClient::sendSignalInterface()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "SignalInterface");
    obj->setProperty("simulationId", juce::String(simulationId_));
    obj->setProperty("simulationName", juce::String(simulationName_));
    obj->setProperty("signalInterfaceRevision", signalInterfaceRevision_);

    juce::Array<juce::var> arr;
    for (const auto& s : signals_)
        arr.add(s.toJson());
    obj->setProperty("signals", arr);

    sendFrame(juce::var(obj));
    log("DEWB -> SignalInterface sent (" + std::to_string(signals_.size()) + " signals)");
}

void DcadMotionClient::sendBindingAccept()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "BindingAccept");
    obj->setProperty("modelId", juce::String(modelId_));
    obj->setProperty("interfaceRevision", interfaceRevision_);
    obj->setProperty("profileRevision", profileRevision_);
    obj->setProperty("simulationId", juce::String(simulationId_));
    obj->setProperty("signalInterfaceRevision", signalInterfaceRevision_);

    sendFrame(juce::var(obj));
    log("DEWB -> BindingAccept sent");
}

void DcadMotionClient::sendBindingReject(int profileRevision, const std::vector<ValidationError>& errors)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "BindingReject");
    obj->setProperty("profileRevision", profileRevision);

    juce::Array<juce::var> errArr;
    for (const auto& e : errors)
        errArr.add(e.toJson());
    obj->setProperty("errors", errArr);

    sendFrame(juce::var(obj));
    log("DEWB -> BindingReject sent");
}

void DcadMotionClient::sendSynchronizeState(const std::string& lifecycle, bool newTimeline)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (acceptedMappings_.empty())
        return;

    int newGen = timelineGeneration_;
    uint64_t newSeq = sequence_;
    auto stagedRateIntegrals = rateIntegrals_;
    auto stagedCompanionOrigins = companionTimelineOrigins_;

    if (newTimeline)
    {
        newGen++;
        newSeq = 0;
        stagedCompanionOrigins.clear();
        for (const auto& m : acceptedMappings_)
        {
            if (m.hasInitialCoordinate)
                stagedRateIntegrals[m.joint] = m.initialCoordinate;
            else
                stagedRateIntegrals[m.joint] = 0.0;
        }
    }

    sim_panel::StateSnapshot snap;
    int64_t simTimePs = 0;
    if (session_)
    {
        snap = session_->getSnapshot();
        simTimePs = (int64_t)std::round(snap.simTime * (double)kTicksPerSecond);
    }
    else
    {
        simTimePs = currentSimTimePs_;
        snap.simTime = (double)currentSimTimePs_ / (double)kTicksPerSecond;
    }

    std::vector<EvaluatedJoint> joints;
    if (!computeJointCoordinates(joints, snap, stagedRateIntegrals, stagedCompanionOrigins))
    {
        log("Error: Full-state synchronization aborted due to coordinate evaluation failure: " + lastErrorMessage_);
        return;
    }

    if (joints.size() != acceptedMappings_.size())
    {
        lastErrorMessage_ = "Evaluated joint count does not match accepted profile mapping count";
        log("Error: " + lastErrorMessage_);
        return;
    }

    // Evaluation succeeded for all joints: commit staged values
    newSeq++;
    timelineGeneration_ = newGen;
    sequence_ = newSeq;
    currentSimTimePs_ = simTimePs;
    currentLifecycle_ = lifecycle;
    rateIntegrals_ = std::move(stagedRateIntegrals);
    companionTimelineOrigins_ = std::move(stagedCompanionOrigins);

    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "SynchronizeState");
    obj->setProperty("simulationId", juce::String(simulationId_));
    obj->setProperty("timelineGeneration", timelineGeneration_);
    obj->setProperty("profileRevision", profileRevision_);
    obj->setProperty("sequence", juce::String(std::to_string(sequence_)));
    obj->setProperty("simTimePs", juce::String(std::to_string(currentSimTimePs_)));
    obj->setProperty("microstep", 0);
    obj->setProperty("lifecycle", juce::String(lifecycle));

    juce::Array<juce::var> jArr;
    for (const auto& j : joints)
        jArr.add(j.toJson());
    obj->setProperty("joints", jArr);

    awaitingStateApplied_ = true;
    awaitingSyncSequence_ = std::to_string(sequence_);

    sendFrame(juce::var(obj));
    log("DEWB -> SynchronizeState sent (gen " + std::to_string(timelineGeneration_) + ", seq " + std::to_string(sequence_) + ")");
}

void DcadMotionClient::sendStateFrame(const std::string& lifecycle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (acceptedMappings_.empty() || awaitingStateApplied_)
        return;

    sim_panel::StateSnapshot snap;
    int64_t simTimePs = 0;
    if (session_)
    {
        snap = session_->getSnapshot();
        simTimePs = (int64_t)std::round(snap.simTime * (double)kTicksPerSecond);
    }
    else
    {
        simTimePs = currentSimTimePs_;
        snap.simTime = (double)currentSimTimePs_ / (double)kTicksPerSecond;
    }

    auto stagedRateIntegrals = rateIntegrals_;
    auto stagedCompanionOrigins = companionTimelineOrigins_;

    std::vector<EvaluatedJoint> joints;
    if (!computeJointCoordinates(joints, snap, stagedRateIntegrals, stagedCompanionOrigins))
    {
        // Coordinate computation failed due to missing signal or non-finite value
        return;
    }

    if (joints.size() != acceptedMappings_.size())
    {
        return;
    }

    // Commit staged state
    sequence_++;
    currentSimTimePs_ = simTimePs;
    currentLifecycle_ = lifecycle;
    rateIntegrals_ = std::move(stagedRateIntegrals);
    companionTimelineOrigins_ = std::move(stagedCompanionOrigins);

    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "StateFrame");
    obj->setProperty("simulationId", juce::String(simulationId_));
    obj->setProperty("timelineGeneration", timelineGeneration_);
    obj->setProperty("profileRevision", profileRevision_);
    obj->setProperty("sequence", juce::String(std::to_string(sequence_)));
    obj->setProperty("simTimePs", juce::String(std::to_string(currentSimTimePs_)));
    obj->setProperty("microstep", 0);
    obj->setProperty("lifecycle", juce::String(lifecycle));

    juce::Array<juce::var> jArr;
    for (const auto& j : joints)
        jArr.add(j.toJson());
    obj->setProperty("joints", jArr);

    sendFrame(juce::var(obj));
}

void DcadMotionClient::sendHeartbeat()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "LinkStatus");
    obj->setProperty("state", juce::String(currentLifecycle_));
    obj->setProperty("severity", "info");
    obj->setProperty("code", "heartbeat");
    obj->setProperty("message", "");
    sendFrame(juce::var(obj));
}

void DcadMotionClient::sendGoodbye()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", "LinkStatus");
    obj->setProperty("state", juce::String(currentLifecycle_));
    obj->setProperty("severity", "info");
    obj->setProperty("code", "goodbye");
    obj->setProperty("message", "DEWB disconnecting");
    sendFrame(juce::var(obj));
}

void DcadMotionClient::sendLifecycleResult(const LifecycleExecutionResult& res)
{
    juce::var json = res.toJson(simulationId_, profileRevision_);
    sendFrame(json);
    log("DEWB -> LifecycleResult " + res.command + " outcome: " + res.outcome);
}

void DcadMotionClient::publishCurrentStateFrame()
{
    sendStateFrame(currentLifecycle_);
}

} // namespace dcad_motion

#include <JuceHeader.h>
#include "DcadMotionClient.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace {

int totalChecks = 0;
int passedChecks = 0;

#define TEST_CHECK(condition, message) \
    do { \
        ++totalChecks; \
        if (condition) { \
            ++passedChecks; \
        } else { \
            std::cerr << "FAILED: " << message << " (" << #condition << ")" << std::endl; \
        } \
    } while (0)

struct PipeTestGuard
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    std::shared_ptr<dcad_motion::DcadMotionClient> client;

    ~PipeTestGuard()
    {
        if (client)
            client->disconnect(false);
        if (hPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
    }
};

// Helper: connect server pipe with strict timeout using OVERLAPPED I/O
bool boundedConnectServerPipe(HANDLE hPipe, std::chrono::milliseconds timeoutBudget)
{
    HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvent) return false;

    OVERLAPPED ov = { 0 };
    ov.hEvent = hEvent;

    BOOL connRet = ConnectNamedPipe(hPipe, &ov);
    bool ok = false;
    if (connRet)
    {
        ok = true;
    }
    else
    {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED)
        {
            ok = true;
        }
        else if (err == ERROR_IO_PENDING)
        {
            DWORD waitRes = WaitForSingleObject(hEvent, (DWORD)timeoutBudget.count());
            if (waitRes == WAIT_OBJECT_0)
            {
                DWORD bytes = 0;
                ok = GetOverlappedResult(hPipe, &ov, &bytes, FALSE);
            }
            else
            {
                CancelIo(hPipe);
                std::cerr << "FAILED: ConnectNamedPipe timed out after " << timeoutBudget.count() << " ms" << std::endl;
            }
        }
        else
        {
            std::cerr << "FAILED: ConnectNamedPipe error " << err << std::endl;
        }
    }

    CloseHandle(hEvent);
    return ok;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto testSuiteStart = std::chrono::steady_clock::now();
    std::cout << "=== Running Bounded DCAD Motion Link Client Unit Tests ===" << std::endl;

    // -------------------------------------------------------------------------
    // Overall Run Watchdog: 30-second budget safeguard
    // -------------------------------------------------------------------------
    std::atomic<bool> suiteFinished { false };
    std::thread watchdogThread([&suiteFinished, testSuiteStart] {
        const auto deadline = testSuiteStart + std::chrono::seconds(30);
        while (!suiteFinished.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!suiteFinished.load())
        {
            std::cerr << "\nFATAL: Whole-run timeout budget (30s) exceeded! Aborting test suite." << std::endl;
            std::exit(2);
        }
    });

    // -------------------------------------------------------------------------
    // 1. Test Unit-to-SI factor calculation
    // -------------------------------------------------------------------------
    std::cout << "[STAGE] Step 1: Unit conversions" << std::endl;
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "rad") - 1.0) < 1e-12, "angle rad = 1");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "deg") - (3.14159265358979323846 / 180.0)) < 1e-12, "angle deg factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angular_velocity", "rpm") - (2.0 * 3.14159265358979323846 / 60.0)) < 1e-12, "angular_velocity rpm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("position", "mm") - 0.001) < 1e-12, "position mm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("linear_velocity", "mm/s") - 0.001) < 1e-12, "linear_velocity mm/s factor");
    TEST_CHECK(dcad_motion::isRateQuantity("angular_velocity"), "angular_velocity is rate");
    TEST_CHECK(dcad_motion::isRateQuantity("linear_velocity"), "linear_velocity is rate");
    TEST_CHECK(!dcad_motion::isRateQuantity("position"), "position is state");
    TEST_CHECK(!dcad_motion::isRateQuantity("angle"), "angle is state");

    // -------------------------------------------------------------------------
    // 2. Test Client instantiation and signal catalog from active session
    // -------------------------------------------------------------------------
    std::cout << "[STAGE] Step 2: Signal catalog" << std::endl;
    auto client = std::make_shared<dcad_motion::DcadMotionClient>();
    TEST_CHECK(client != nullptr, "Client created");

    auto session = std::make_shared<sim_panel::SystemSimulationSession>();
    session->setupDefaultMotorSystem();
    client->attachSession(session);

    auto signals = client->getSignalCatalog();
    TEST_CHECK(!signals.empty(), "Signal catalog populated from active session");

    bool hasMotorSpeed = false;
    bool hasMotorAngle = false;
    bool hasFakeCarriage = false;
    bool hasFakeSpindle = false;
    for (const auto& s : signals)
    {
        if (s.id == "motor.speed") hasMotorSpeed = true;
        if (s.id == "motor.angle") hasMotorAngle = true;
        if (s.id == "carriage.position") hasFakeCarriage = true;
        if (s.id == "spindle.speed") hasFakeSpindle = true;
    }
    TEST_CHECK(hasMotorSpeed, "motor.speed present in catalog");
    TEST_CHECK(hasMotorAngle, "motor.angle present in catalog");
    TEST_CHECK(!hasFakeCarriage, "carriage.position NOT advertised (no fake motion)");
    TEST_CHECK(!hasFakeSpindle, "spindle.speed NOT advertised (no fake motion)");

    // -------------------------------------------------------------------------
    // 3. Test Session Attachment & Lifecycle Command Execution (with Deduplication)
    // -------------------------------------------------------------------------
    std::cout << "[STAGE] Step 3: Local lifecycle commands & deduplication" << std::endl;
    auto status = client->getStatus();
    TEST_CHECK(status.lifecycle == "stopped", "Initial lifecycle stopped");

    // Execute Start
    auto resStart = client->executeLifecycleCommand("start", 0, "test-req-start-1");
    TEST_CHECK(resStart.outcome == "applied", "Start command applied");
    TEST_CHECK(resStart.lifecycle == "running", "Lifecycle is running");

    // Deduplication check: re-running identical requestId must return stored result without error
    auto resStartDup = client->executeLifecycleCommand("start", 0, "test-req-start-1");
    TEST_CHECK(resStartDup.outcome == "applied", "Duplicate Start returned stored applied result");
    TEST_CHECK(resStartDup.requestId == "test-req-start-1", "Duplicate Start preserved requestId");

    // Execute Pause
    auto resPause = client->executeLifecycleCommand("pause", 0, "test-req-pause-1");
    TEST_CHECK(resPause.outcome == "applied", "Pause command applied");
    TEST_CHECK(resPause.lifecycle == "paused", "Lifecycle is paused");

    // Duplicate Pause
    auto resPauseDup = client->executeLifecycleCommand("pause", 0, "test-req-pause-1");
    TEST_CHECK(resPauseDup.outcome == "applied", "Duplicate Pause returned stored result");

    // Execute Step from Paused
    auto resStep = client->executeLifecycleCommand("step", 5000000000LL, "test-req-step-1"); // 5 ms in ps
    TEST_CHECK(resStep.outcome == "applied", "Step command applied");
    TEST_CHECK(resStep.lifecycle == "paused", "Lifecycle stays paused after step");

    // Duplicate Step must deduplicate and NOT advance time again
    auto resStepDup = client->executeLifecycleCommand("step", 5000000000LL, "test-req-step-1");
    TEST_CHECK(resStepDup.outcome == "applied", "Duplicate Step returned stored result");

    // Execute Resume
    auto resResume = client->executeLifecycleCommand("resume", 0, "test-req-resume-1");
    TEST_CHECK(resResume.outcome == "applied", "Resume command applied");
    TEST_CHECK(resResume.lifecycle == "running", "Lifecycle is running after resume");

    // Execute Stop
    auto resStop = client->executeLifecycleCommand("stop", 0, "test-req-stop-1");
    TEST_CHECK(resStop.outcome == "applied", "Stop command applied");
    TEST_CHECK(resStop.lifecycle == "stopped", "Lifecycle is stopped after stop");

    // Execute Reset
    auto resReset = client->executeLifecycleCommand("reset", 0, "test-req-reset-1");
    TEST_CHECK(resReset.outcome == "applied", "Reset command applied");
    TEST_CHECK(resReset.lifecycle == "stopped", "Lifecycle is stopped at t=0 after reset");
    TEST_CHECK(resReset.simTimePs == 0, "Simulation time is 0 after reset");

    // -------------------------------------------------------------------------
    // 4. Live Bounded Named Pipe Loopback
    // -------------------------------------------------------------------------
    std::cout << "[STAGE] Step 4: Named pipe IPC communication" << std::endl;
    std::string pipeName = "DjehutiCAD.Motion.Test." + std::to_string(GetCurrentProcessId());
    std::string pipePath = "\\\\.\\pipe\\" + pipeName;

    // Use FILE_FLAG_OVERLAPPED to prevent blocking pipe operations
    HANDLE hServerPipe = CreateNamedPipeA(
        pipePath.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        65536,
        65536,
        1000,
        NULL
    );
    TEST_CHECK(hServerPipe != INVALID_HANDLE_VALUE, "Server named pipe created for loopback check");

    PipeTestGuard guard;
    guard.hPipe = hServerPipe;
    guard.client = client;

    if (hServerPipe != INVALID_HANDLE_VALUE)
    {
        // Connect with bounded 3-second absolute deadline
        std::atomic<bool> connectOk { false };
        std::thread clientConnectThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            connectOk = client->connectToPipe(pipePath);
        });

        BOOL connected = boundedConnectServerPipe(hServerPipe, std::chrono::milliseconds(3000));
        TEST_CHECK(connected, "Server accepted pipe connection");

        if (clientConnectThread.joinable())
            clientConnectThread.join();
        TEST_CHECK(connectOk.load(), "Client connected to test pipe");

        // Helper: send frame from server
        auto serverSendFrame = [&hServerPipe](const juce::var& payload) {
            auto s = juce::JSON::toString(payload, true);
            juce::MemoryOutputStream m;
            m.writeInt(dcad_motion::kMagicNumber);
            m.writeInt((int)s.getNumBytesAsUTF8());
            m.write(s.toRawUTF8(), s.getNumBytesAsUTF8());

            OVERLAPPED ov = { 0 };
            ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            DWORD written = 0;
            if (!WriteFile(hServerPipe, m.getData(), (DWORD)m.getDataSize(), &written, &ov))
            {
                if (GetLastError() == ERROR_IO_PENDING)
                {
                    WaitForSingleObject(ov.hEvent, 1000);
                    GetOverlappedResult(hServerPipe, &ov, &written, FALSE);
                }
            }
            CloseHandle(ov.hEvent);
        };

        std::vector<juce::var> received;
        // Bounded frame reader: strictly bounded by stage budget
        auto serverWaitForMessage = [&hServerPipe, &received](
            const std::string& stageName,
            const std::string& expectedType,
            std::chrono::milliseconds budget,
            juce::var& outMsg,
            std::function<bool(const juce::var&)> predicate = nullptr) -> bool
        {
            const auto stageStart = std::chrono::steady_clock::now();
            const auto stageDeadline = stageStart + budget;

            while (std::chrono::steady_clock::now() < stageDeadline)
            {
                DWORD bytesAvail = 0;
                if (PeekNamedPipe(hServerPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail >= 8)
                {
                    uint32_t header[2] = {0, 0};
                    OVERLAPPED ovHdr = { 0 };
                    ovHdr.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                    DWORD readHeader = 0;
                    BOOL hOk = ReadFile(hServerPipe, header, 8, &readHeader, &ovHdr);
                    if (!hOk && GetLastError() == ERROR_IO_PENDING)
                    {
                        WaitForSingleObject(ovHdr.hEvent, 500);
                        hOk = GetOverlappedResult(hServerPipe, &ovHdr, &readHeader, FALSE);
                    }
                    CloseHandle(ovHdr.hEvent);

                    if (!hOk || readHeader < 8)
                        return false;
                    if (header[0] != dcad_motion::kMagicNumber)
                        return false;

                    uint32_t len = header[1];
                    std::vector<char> buf(len);
                    DWORD totalRead = 0;
                    const auto payloadDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
                    while (totalRead < len && std::chrono::steady_clock::now() < payloadDeadline)
                    {
                        OVERLAPPED ovChunk = { 0 };
                        ovChunk.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                        DWORD chunk = 0;
                        BOOL cOk = ReadFile(hServerPipe, buf.data() + totalRead, len - totalRead, &chunk, &ovChunk);
                        if (!cOk && GetLastError() == ERROR_IO_PENDING)
                        {
                            WaitForSingleObject(ovChunk.hEvent, 200);
                            cOk = GetOverlappedResult(hServerPipe, &ovChunk, &chunk, FALSE);
                        }
                        CloseHandle(ovChunk.hEvent);

                        if (!cOk || chunk == 0)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            continue;
                        }
                        totalRead += chunk;
                    }
                    if (totalRead < len)
                        return false;

                    auto parsed = juce::JSON::parse(juce::String::fromUTF8(buf.data(), (int)len));
                    if (parsed.isObject())
                    {
                        received.push_back(parsed);
                        std::string type = parsed.getProperty("type", {}).toString().toStdString();
                        if (type == expectedType)
                        {
                            if (!predicate || predicate(parsed))
                            {
                                outMsg = parsed;
                                return true;
                            }
                        }
                        // Unrelated frames (e.g. heartbeat or streaming StateFrame) are skipped
                        // without resetting stageDeadline
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stageStart).count();
            std::cerr << "TIMEOUT: [" << stageName << "] Timed out after " << elapsed << " ms waiting for message '"
                      << expectedType << "' (budget: " << budget.count() << " ms)" << std::endl;
            return false;
        };

        // ---------------------------------------------------------------------
        // 4.1 Handshake (Hello, MechanicalInterface, SignalInterface)
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.1: Handshake" << std::endl;
        juce::var helloMsg;
        TEST_CHECK(serverWaitForMessage("ReadHello", "Hello", std::chrono::milliseconds(3000), helloMsg), "Server received Hello");
        TEST_CHECK(helloMsg.getProperty("role", {}).toString() == "dewb", "Role is dewb");

        // Server sends Hello and MechanicalInterface
        auto* srvHello = new juce::DynamicObject();
        srvHello->setProperty("type", "Hello");
        srvHello->setProperty("protocol", "djehuti.motion");
        srvHello->setProperty("protocolVersion", 1);
        srvHello->setProperty("role", "dcad");
        srvHello->setProperty("capabilities", juce::var(juce::Array<juce::var> { "mechanicalInterface.v1", "bindingProposal.v1", "stateFrame.v1", "heartbeat.v1", "lifecycleControl.v1" }));
        serverSendFrame(juce::var(srvHello));

        auto* srvMech = new juce::DynamicObject();
        srvMech->setProperty("type", "MechanicalInterface");
        srvMech->setProperty("modelId", "cad-model-uuid-1234");
        srvMech->setProperty("documentName", "TestCarriageAssembly.step");
        srvMech->setProperty("interfaceRevision", 1);
        srvMech->setProperty("profileRevision", 1);
        srvMech->setProperty("joints", juce::var(juce::Array<juce::var>()));
        serverSendFrame(juce::var(srvMech));

        juce::var sigIntMsg;
        TEST_CHECK(serverWaitForMessage("ReadSignalInterface", "SignalInterface", std::chrono::milliseconds(3000), sigIntMsg), "Server received SignalInterface");

        // ---------------------------------------------------------------------
        // 4.2 Validation: reject unknown signal
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.2: Unknown signal rejection" << std::endl;
        auto* badProp = new juce::DynamicObject();
        badProp->setProperty("type", "BindingProposal");
        badProp->setProperty("modelId", "cad-model-uuid-1234");
        badProp->setProperty("interfaceRevision", 1);
        badProp->setProperty("profileRevision", 1);
        badProp->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        badProp->setProperty("signalInterfaceRevision", (int)sigIntMsg.getProperty("signalInterfaceRevision", 1));
        juce::Array<juce::var> badMappings;
        auto* bm1 = new juce::DynamicObject();
        bm1->setProperty("joint", "joint-carriage");
        bm1->setProperty("signal", "carriage.position");
        bm1->setProperty("sourceQuantity", "position");
        bm1->setProperty("sourceUnit", "mm");
        bm1->setProperty("jointQuantity", "position");
        bm1->setProperty("jointUnit", "m");
        bm1->setProperty("unitToSI", 0.001);
        bm1->setProperty("scale", 1.0);
        bm1->setProperty("offset", 0.0);
        badMappings.add(juce::var(bm1));
        badProp->setProperty("mappings", badMappings);
        serverSendFrame(juce::var(badProp));

        juce::var rejectMsg;
        TEST_CHECK(serverWaitForMessage("ReadBindingReject", "BindingReject", std::chrono::milliseconds(3000), rejectMsg), "Server received BindingReject");
        const auto* errs = rejectMsg.getProperty("errors", {}).getArray();
        TEST_CHECK(errs != nullptr && !errs->isEmpty(), "BindingReject contains errors list");
        if (errs && !errs->isEmpty())
        {
            TEST_CHECK((*errs)[0].getProperty("code", {}).toString() == "unknownSignal", "Rejection error code is unknownSignal");
        }

        // ---------------------------------------------------------------------
        // 4.3 Valid binding proposal with non-zero initialCoordinate (0.5 rad)
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.3: Valid proposal with initialCoordinate = 0.5 rad" << std::endl;
        auto* prop = new juce::DynamicObject();
        prop->setProperty("type", "BindingProposal");
        prop->setProperty("modelId", "cad-model-uuid-1234");
        prop->setProperty("interfaceRevision", 1);
        prop->setProperty("profileRevision", 2);
        prop->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        prop->setProperty("signalInterfaceRevision", (int)sigIntMsg.getProperty("signalInterfaceRevision", 1));

        juce::Array<juce::var> mappings;
        auto* m1 = new juce::DynamicObject();
        m1->setProperty("joint", "joint-spindle");
        m1->setProperty("signal", "motor.speed");
        m1->setProperty("sourceQuantity", "angular_velocity");
        m1->setProperty("sourceUnit", "rad/s");
        m1->setProperty("jointQuantity", "angle");
        m1->setProperty("jointUnit", "rad");
        m1->setProperty("unitToSI", 1.0);
        m1->setProperty("scale", 1.0);
        m1->setProperty("offset", 0.0);
        m1->setProperty("initialCoordinate", 0.5); // non-zero initial coordinate
        mappings.add(juce::var(m1));
        prop->setProperty("mappings", mappings);
        serverSendFrame(juce::var(prop));

        juce::var acceptMsg;
        TEST_CHECK(serverWaitForMessage("ReadBindingAccept", "BindingAccept", std::chrono::milliseconds(3000), acceptMsg), "Server received BindingAccept");

        // ---------------------------------------------------------------------
        // 4.4 Full synchronization check: verify initial coordinate = 0.5 rad
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.4: Full state synchronization check" << std::endl;
        juce::var syncMsg;
        TEST_CHECK(serverWaitForMessage("ReadSynchronizeState", "SynchronizeState", std::chrono::milliseconds(3000), syncMsg), "Server received SynchronizeState");
        auto seq = syncMsg.getProperty("sequence", {}).toString();
        int initialTimeline = (int)syncMsg.getProperty("timelineGeneration", 0);
        TEST_CHECK(initialTimeline == 1, "Initial timelineGeneration is 1");

        const auto* jointsArr = syncMsg.getProperty("joints", {}).getArray();
        TEST_CHECK(jointsArr != nullptr && !jointsArr->isEmpty(), "SynchronizeState joints array present");
        if (jointsArr && !jointsArr->isEmpty())
        {
            double coord = (double)(*jointsArr)[0].getProperty("coordinate", 0.0);
            TEST_CHECK(std::abs(coord - 0.5) < 1e-6, "Initial coordinate 0.5 rad preserved in SynchronizeState");
        }

        // Acknowledge with StateApplied
        auto* applied = new juce::DynamicObject();
        applied->setProperty("type", "StateApplied");
        applied->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        applied->setProperty("timelineGeneration", initialTimeline);
        applied->setProperty("profileRevision", 2);
        applied->setProperty("sequence", seq);
        applied->setProperty("duplicate", false);
        serverSendFrame(juce::var(applied));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        TEST_CHECK(client->getStatus().state == dcad_motion::LinkState::Streaming, "Client transitioned to Streaming after StateApplied");

        // Lifecycle acceptance is exercised exclusively over the production pipe.
        std::cout << "[STAGE] Wire lifecycle sequence and publication ordering" << std::endl;
        int requestNumber = 0;
        auto ticksOf = [](const juce::var& v) {
            return v.getProperty("simTimePs", "0").toString().getLargeIntValue();
        };
        auto makeCommand = [&](const char* command, const char* duration = nullptr) {
            auto* o = new juce::DynamicObject();
            o->setProperty("type", "LifecycleCommand");
            o->setProperty("requestId", "wire-" + juce::String(++requestNumber));
            o->setProperty("modelId", "cad-model-uuid-1234");
            o->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
            o->setProperty("profileRevision", client->getStatus().profileRevision);
            o->setProperty("timelineGeneration", client->getStatus().timelineGeneration);
            o->setProperty("command", command);
            if (duration) o->setProperty("stepDurationPs", duration);
            return juce::var(o);
        };
        auto waitResult = [&](const juce::var& command, juce::var& result, const char* outcome = nullptr) {
            return serverWaitForMessage("LifecycleResult", "LifecycleResult", std::chrono::milliseconds(3000), result,
                [&](const juce::var& m) {
                    return m.getProperty("requestId", {}) == command.getProperty("requestId", {})
                        && (outcome ? m.getProperty("outcome", {}).toString() == outcome
                                    : m.getProperty("outcome", {}).toString() != "accepted");
                });
        };
        auto verifyPublication = [&](const juce::var& result) {
            const auto seq = result.getProperty("stateSequence", {}).toString();
            bool stateBeforeResult = false;
            for (const auto& m : received)
            {
                const auto type = m.getProperty("type", {}).toString();
                if ((type == "StateFrame" || type == "SynchronizeState")
                    && m.getProperty("sequence", {}).toString() == seq
                    && m.getProperty("timelineGeneration", {}) == result.getProperty("timelineGeneration", {})
                    && ticksOf(m) == ticksOf(result)) stateBeforeResult = true;
                if (type == "LifecycleResult" && m.getProperty("requestId", {}) == result.getProperty("requestId", {})
                    && m.getProperty("outcome", {}).toString() == "applied") break;
            }
            TEST_CHECK(stateBeforeResult, "Complete matching state precedes applied lifecycle result");
        };
        auto roundTrip = [&](const juce::var& command, const char* outcome = "applied", const char* code = nullptr) {
            serverSendFrame(command);
            juce::var result;
            TEST_CHECK(waitResult(command, result), "Correlated lifecycle result received");
            TEST_CHECK(result.getProperty("outcome", {}).toString() == outcome, "Lifecycle outcome matches expectation");
            TEST_CHECK(result.getProperty("command", {}) == command.getProperty("command", {}), "Result command is correlated");
            TEST_CHECK(result.getProperty("simulationId", {}) == sigIntMsg.getProperty("simulationId", {}), "Result simulation identity matches");
            if (code) TEST_CHECK(result.getProperty("code", {}).toString() == code, "Explicit rejection code matches");
            if (juce::String(outcome) == "applied") verifyPublication(result);
            return result;
        };
        auto ackSync = [&](const juce::var& sync) {
            auto* ack = new juce::DynamicObject();
            ack->setProperty("type", "StateApplied");
            for (const auto* field : {"simulationId", "profileRevision", "timelineGeneration", "sequence"})
                ack->setProperty(field, sync.getProperty(field, {}));
            serverSendFrame(juce::var(ack));
        };
        session->setExecutionRate(0.001); // pacing interval is 5 seconds; lifecycle must interrupt it
        const auto startCommand = makeCommand("start");
        auto startResult = roundTrip(startCommand);
        TEST_CHECK(startResult.getProperty("lifecycle", {}).toString() == "running", "Start runs");
        roundTrip(makeCommand("start"), "rejected", "invalidTransition");
        const auto pauseWall = std::chrono::steady_clock::now();
        auto pauseResult = roundTrip(makeCommand("pause"));
        TEST_CHECK(std::chrono::steady_clock::now() - pauseWall < std::chrono::seconds(1), "Pause promptly interrupts slow wall-clock pacing");
        const auto frozen = session->getSnapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        TEST_CHECK(session->getSnapshot().simTime == frozen.simTime, "Pause result means worker is quiescent");
        TEST_CHECK(session->getSnapshot().numericSignals == frozen.numericSignals, "Paused numerical state is preserved");
        const auto stepCommand = makeCommand("step", "7000000000");
        auto stepResult = roundTrip(stepCommand);
        TEST_CHECK(ticksOf(stepResult) - ticksOf(pauseResult) == 7000000000LL, "Step advances exactly requested 7 ms before result");
        TEST_CHECK(stepResult.getProperty("lifecycle", {}).toString() == "paused", "Step remains paused");
        const auto stepped = session->getSnapshot();
        auto duplicateStep = roundTrip(stepCommand);
        TEST_CHECK(ticksOf(duplicateStep) == ticksOf(stepResult), "Retry returns original step result");
        TEST_CHECK(session->getSnapshot().simTime == stepped.simTime, "Step retry never executes twice");
        for (const auto* bad : {"0", "-1", "abc", "9223372036854775808", "1.5", ""})
            roundTrip(makeCommand("step", bad), "rejected", "invalidStep");
        roundTrip(makeCommand("step"), "rejected", "invalidStep");
        roundTrip(makeCommand("unsupported"), "rejected", "unknownCommand");
        auto stale = makeCommand("resume");
        stale.getDynamicObject()->setProperty("timelineGeneration", initialTimeline - 1);
        roundTrip(stale, "rejected", "staleTimeline");
        auto wrongBinding = makeCommand("resume");
        wrongBinding.getDynamicObject()->setProperty("modelId", "another-model");
        roundTrip(wrongBinding, "rejected", "staleBinding");
        roundTrip(makeCommand("resume"));
        const auto stopWall = std::chrono::steady_clock::now();
        auto stopResult = roundTrip(makeCommand("stop"));
        TEST_CHECK(std::chrono::steady_clock::now() - stopWall < std::chrono::seconds(1), "Stop promptly interrupts slow wall-clock pacing");
        session->setExecutionRate(1.0);
        const auto stopped = session->getSnapshot();
        TEST_CHECK(!stopped.running && ticksOf(stopResult) == (int64_t)std::llround(stopped.simTime * dcad_motion::kTicksPerSecond), "Stop retains committed final time");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        TEST_CHECK(session->getSnapshot().numericSignals == stopped.numericSignals, "Stop retains final numerical state");

        auto resetCommand = makeCommand("reset");
        serverSendFrame(resetCommand);
        juce::var resetSync, resetAccepted, resetResult;
        TEST_CHECK(serverWaitForMessage("ResetSync", "SynchronizeState", std::chrono::milliseconds(3000), resetSync), "Reset publishes full new timeline");
        TEST_CHECK(waitResult(resetCommand, resetAccepted, "accepted"), "Reset is pending until CAD applies state");
        TEST_CHECK(ticksOf(resetSync) == 0 && (int)resetSync.getProperty("timelineGeneration", 0) > initialTimeline, "Reset initial time on new timeline");
        // A stale acknowledgement with the same sequence must not complete reset.
        auto wrongAck = resetSync.clone();
        wrongAck.getDynamicObject()->setProperty("type", "StateApplied");
        wrongAck.getDynamicObject()->setProperty("simulationId", "obsolete-simulation");
        serverSendFrame(wrongAck);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        TEST_CHECK(client->getStatus().state == dcad_motion::LinkState::Synchronizing, "Wrong simulation acknowledgement ignored");
        wrongAck.getDynamicObject()->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        wrongAck.getDynamicObject()->setProperty("timelineGeneration", initialTimeline);
        serverSendFrame(wrongAck);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        TEST_CHECK(client->getStatus().state == dcad_motion::LinkState::Synchronizing, "Wrong timeline acknowledgement ignored");
        auto busy = makeCommand("start");
        roundTrip(busy, "rejected", "busy");
        ackSync(resetSync);
        TEST_CHECK(waitResult(resetCommand, resetResult), "Reset applied after exact state acknowledgement");
        TEST_CHECK(resetResult.getProperty("outcome", {}).toString() == "applied" && ticksOf(resetResult) == 0, "Reset completes at zero");
        verifyPublication(resetResult);
        roundTrip(resetCommand); // identical request retains its old originating timeline

        // Initial-state step must never race an initial free-running worker chunk.
        auto initialStep = roundTrip(makeCommand("step", "1000000000"));
        TEST_CHECK(ticksOf(initialStep) == 1000000000LL, "Step from initial stopped state advances exactly 1 ms");
        roundTrip(makeCommand("stop"));
        // Fresh start must synchronize t=0 before starting the worker.
        auto restartCommand = makeCommand("start");
        serverSendFrame(restartCommand);
        juce::var restartSync, restartAccepted, restartResult;
        TEST_CHECK(serverWaitForMessage("RestartSync", "SynchronizeState", std::chrono::milliseconds(3000), restartSync), "Fresh Start synchronizes a new timeline");
        TEST_CHECK(waitResult(restartCommand, restartAccepted, "accepted"), "Fresh Start awaits CAD");
        TEST_CHECK(ticksOf(restartSync) == 0 && !session->isRunning(), "Fresh Start prepared at zero and does not run before acknowledgement");
        ackSync(restartSync);
        TEST_CHECK(waitResult(restartCommand, restartResult), "Fresh Start finishes after CAD acknowledgement");
        TEST_CHECK(restartResult.getProperty("outcome", {}).toString() == "applied", "Fresh Start applied");
        verifyPublication(restartResult);
        auto pausedAgain = roundTrip(makeCommand("pause"));
        initialTimeline = (int)pausedAgain.getProperty("timelineGeneration", 0);
        int64_t pausedTimePs = ticksOf(pausedAgain);
        const double pausedCoordinate = client->getSession()->getSnapshot().numericSignals.at("motor.angle");

        // ---------------------------------------------------------------------
        // 4.6 Reconnection & State Preservation (Requirement 4)
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.6: Reconnect & coordinate preservation" << std::endl;
        client->disconnect(false);

        CancelIo(hServerPipe);
        CloseHandle(hServerPipe);
        guard.hPipe = INVALID_HANDLE_VALUE;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        hServerPipe = CreateNamedPipeA(
            pipePath.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            1000,
            NULL
        );
        guard.hPipe = hServerPipe;
        TEST_CHECK(hServerPipe != INVALID_HANDLE_VALUE, "Server named pipe recreated for reconnect check");

        std::atomic<bool> reconOk { false };
        std::thread reconnectThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            reconOk = client->connectToPipe(pipePath);
        });

        BOOL reconConnected = boundedConnectServerPipe(hServerPipe, std::chrono::milliseconds(3000));
        TEST_CHECK(reconConnected, "Server accepted pipe connection after reconnect");
        if (reconnectThread.joinable())
            reconnectThread.join();
        TEST_CHECK(reconOk.load(), "Client reconnected to pipe");

        juce::var reconHello;
        TEST_CHECK(serverWaitForMessage("ReconHello", "Hello", std::chrono::milliseconds(3000), reconHello), "Server received Hello after reconnect");

        auto* srvHello2 = new juce::DynamicObject();
        srvHello2->setProperty("type", "Hello");
        srvHello2->setProperty("protocol", "djehuti.motion");
        srvHello2->setProperty("protocolVersion", 1);
        srvHello2->setProperty("role", "dcad");
        srvHello2->setProperty("capabilities", juce::var(juce::Array<juce::var> { "mechanicalInterface.v1", "bindingProposal.v1", "stateFrame.v1", "heartbeat.v1", "lifecycleControl.v1" }));
        serverSendFrame(juce::var(srvHello2));

        auto* srvMech2 = new juce::DynamicObject();
        srvMech2->setProperty("type", "MechanicalInterface");
        srvMech2->setProperty("modelId", "cad-model-uuid-1234");
        srvMech2->setProperty("documentName", "TestCarriageAssembly.step");
        srvMech2->setProperty("interfaceRevision", 1);
        srvMech2->setProperty("profileRevision", 1);
        srvMech2->setProperty("joints", juce::var(juce::Array<juce::var>()));
        serverSendFrame(juce::var(srvMech2));

        juce::var reconSigInt;
        TEST_CHECK(serverWaitForMessage("ReconSignalInterface", "SignalInterface", std::chrono::milliseconds(3000), reconSigInt), "Server received SignalInterface after reconnect");

        // Re-propose binding (profileRevision 3)
        auto* reconProp = new juce::DynamicObject();
        reconProp->setProperty("type", "BindingProposal");
        reconProp->setProperty("modelId", "cad-model-uuid-1234");
        reconProp->setProperty("interfaceRevision", 1);
        reconProp->setProperty("profileRevision", 3);
        reconProp->setProperty("simulationId", reconSigInt.getProperty("simulationId", {}));
        reconProp->setProperty("signalInterfaceRevision", (int)reconSigInt.getProperty("signalInterfaceRevision", 1));
        reconProp->setProperty("mappings", mappings);
        serverSendFrame(juce::var(reconProp));

        juce::var reconAccept;
        TEST_CHECK(serverWaitForMessage("ReconBindingAccept", "BindingAccept", std::chrono::milliseconds(3000), reconAccept), "Server received BindingAccept after reconnect");

        juce::var reconSync;
        TEST_CHECK(serverWaitForMessage("ReconSynchronizeState", "SynchronizeState", std::chrono::milliseconds(3000), reconSync), "Server received SynchronizeState after reconnect");
        int reconTimeline = (int)reconSync.getProperty("timelineGeneration", 0);
        TEST_CHECK(reconTimeline == initialTimeline, "Timeline generation preserved across reconnect (not reset)");
        int64_t reconSimTime = (int64_t)std::stoll(reconSync.getProperty("simTimePs", "0").toString().toStdString());
        TEST_CHECK(reconSimTime == pausedTimePs, "Simulation time preserved across reconnect (not reset)");

        const auto* reconJoints = reconSync.getProperty("joints", {}).getArray();
        TEST_CHECK(reconJoints != nullptr && !reconJoints->isEmpty(), "Reconnect SynchronizeState has joints");
        if (reconJoints && !reconJoints->isEmpty())
        {
            double rCoord = (double)(*reconJoints)[0].getProperty("coordinate", 0.0);
            TEST_CHECK(std::abs(rCoord - (0.5 + pausedCoordinate)) < 1e-6, "Coordinate preserved across reconnect without reset");
        }

        ackSync(reconSync);
        // Completed requests survive reconnection; reusing the original step id must not step again.
        auto retryAfterReconnect = stepCommand.clone();
        retryAfterReconnect.getDynamicObject()->setProperty("profileRevision", 3);
        serverSendFrame(retryAfterReconnect);
        juce::var reconnectRetry;
        TEST_CHECK(waitResult(retryAfterReconnect, reconnectRetry), "Retry after reconnect returns cached result");
        TEST_CHECK(ticksOf(reconnectRetry) == ticksOf(stepResult), "Reconnect retry preserves original step outcome");
        TEST_CHECK((int64_t)std::llround(session->getSnapshot().simTime * dcad_motion::kTicksPerSecond) == pausedTimePs, "Reconnect retry does not execute a second step");

        // Missing acknowledgement has a bounded terminal result and cannot restart on late ack.
        auto timeoutReset = makeCommand("reset");
        timeoutReset.getDynamicObject()->setProperty("profileRevision", 3);
        serverSendFrame(timeoutReset);
        juce::var timeoutSync, timeoutAccepted, timeoutResult;
        TEST_CHECK(serverWaitForMessage("TimeoutSync", "SynchronizeState", std::chrono::milliseconds(3000), timeoutSync), "Timeout fixture receives reset state");
        TEST_CHECK(waitResult(timeoutReset, timeoutAccepted, "accepted"), "Timeout fixture receives accepted");
        TEST_CHECK(serverWaitForMessage("AckTimeout", "LifecycleResult", std::chrono::milliseconds(6500), timeoutResult,
            [&](const juce::var& m) { return m.getProperty("requestId", {}) == timeoutReset.getProperty("requestId", {}) && m.getProperty("outcome", {}).toString() == "rejected"; }), "Missing StateApplied returns bounded rejection");
        TEST_CHECK(timeoutResult.getProperty("code", {}).toString() == "faulted", "Timeout explains unconfirmed CAD state");
        ackSync(timeoutSync);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        TEST_CHECK(!session->isRunning() && client->getStatus().state == dcad_motion::LinkState::Error, "Late acknowledgement cannot revive timed-out transaction");

        auto reconnectBound = [&] {
            client->disconnect(false);
            CloseHandle(hServerPipe);
            guard.hPipe = hServerPipe = CreateNamedPipeA(pipePath.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 1000, nullptr);
            std::thread connector([&] { client->connectToPipe(pipePath); });
            const bool connected = boundedConnectServerPipe(hServerPipe, std::chrono::milliseconds(3000));
            connector.join();
            TEST_CHECK(connected, "Lifecycle recovery reconnects transport");
            juce::var peerHello;
            TEST_CHECK(serverWaitForMessage("RecoveryHello", "Hello", std::chrono::milliseconds(3000), peerHello), "Recovery starts a fresh handshake");
            auto hello = juce::JSON::parse(R"({"type":"Hello","protocol":"djehuti.motion","protocolVersion":1,"role":"dcad","capabilities":["lifecycleControl.v1"]})");
            serverSendFrame(hello);
            serverSendFrame(juce::JSON::parse(R"({"type":"MechanicalInterface","modelId":"cad-model-uuid-1234","documentName":"Recovery","interfaceRevision":1,"profileRevision":3})"));
            juce::var signals;
            TEST_CHECK(serverWaitForMessage("RecoverySignals", "SignalInterface", std::chrono::milliseconds(3000), signals), "Recovery revalidates signal interface");
            auto proposal = juce::JSON::parse(R"({"type":"BindingProposal","modelId":"cad-model-uuid-1234","interfaceRevision":1,"profileRevision":3})");
            proposal.getDynamicObject()->setProperty("simulationId", signals.getProperty("simulationId", {}));
            proposal.getDynamicObject()->setProperty("mappings", mappings);
            serverSendFrame(proposal);
            juce::var accepted, synchronized;
            TEST_CHECK(serverWaitForMessage("RecoveryAccept", "BindingAccept", std::chrono::milliseconds(3000), accepted), "Recovery rebinds");
            TEST_CHECK(serverWaitForMessage("RecoverySync", "SynchronizeState", std::chrono::milliseconds(3000), synchronized), "Recovery fully synchronizes");
            ackSync(synchronized);
            const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (client->getStatus().state != dcad_motion::LinkState::Streaming && std::chrono::steady_clock::now() < readyDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            TEST_CHECK(client->getStatus().state == dcad_motion::LinkState::Streaming, "Recovery waits for StateApplied");
        };
        reconnectBound();
        roundTrip(makeCommand("start"));
        roundTrip(makeCommand("pause"));
        roundTrip(makeCommand("stop"));
        auto interruptedStart = makeCommand("start");
        serverSendFrame(interruptedStart);
        juce::var interruptedSync, interruptedAccepted;
        TEST_CHECK(serverWaitForMessage("InterruptedStartSync", "SynchronizeState", std::chrono::milliseconds(3000), interruptedSync), "Fresh start prepares state before interrupted synchronization");
        TEST_CHECK(waitResult(interruptedStart, interruptedAccepted, "accepted"), "Interrupted start is pending");
        reconnectBound(); // never acknowledge the old start's synchronization
        TEST_CHECK(!session->isRunning() && session->getSnapshot().simTime == 0.0, "Disconnect cancels deferred start without running");
        roundTrip(interruptedStart, "rejected", "faulted");
        TEST_CHECK(!session->isRunning(), "Retry after reconnect cannot execute an interrupted start");

        // ---------------------------------------------------------------------
        // 4.7 Bounded Deliberate Timeout Check (Requirement 4)
        // Confirms that the harness exits within its deadline and names the failed stage
        // when an expected message is absent.
        // ---------------------------------------------------------------------
        std::cout << "[STAGE] Step 4.7: Deliberate timeout check" << std::endl;
        juce::var dummyMsg;
        auto deliberateStart = std::chrono::steady_clock::now();
        bool deliberateResult = serverWaitForMessage(
            "DeliberateTimeoutCheck", "NonExistentExpectedResponse",
            std::chrono::milliseconds(250), dummyMsg
        );
        auto deliberateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - deliberateStart).count();
        TEST_CHECK(!deliberateResult, "Deliberate timeout test returned false as expected");
        TEST_CHECK(deliberateElapsed >= 240 && deliberateElapsed <= 600, "Deliberate timeout exited promptly within budget");

        // Clean disconnect
        client->disconnect(true);
    }

    suiteFinished = true;
    if (watchdogThread.joinable())
        watchdogThread.join();

    auto testSuiteEnd = std::chrono::steady_clock::now();
    double totalElapsedMs = std::chrono::duration<double, std::milli>(testSuiteEnd - testSuiteStart).count();

    std::cout << "\nTest Summary: " << passedChecks << " / " << totalChecks << " checks passed in "
              << totalElapsedMs << " ms." << std::endl;
    return (passedChecks == totalChecks) ? 0 : 1;
}

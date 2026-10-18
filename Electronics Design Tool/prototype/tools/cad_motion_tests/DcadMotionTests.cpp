#include <JuceHeader.h>
#include "DcadMotionClient.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

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

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "=== Running DCAD Motion Link Client Unit Tests ===" << std::endl;

    std::cout << "[DEBUG] Step 1" << std::endl;
    // 1. Test Unit-to-SI factor calculation
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "rad") - 1.0) < 1e-12, "angle rad = 1");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angle", "deg") - (3.14159265358979323846 / 180.0)) < 1e-12, "angle deg factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("angular_velocity", "rpm") - (2.0 * 3.14159265358979323846 / 60.0)) < 1e-12, "angular_velocity rpm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("position", "mm") - 0.001) < 1e-12, "position mm factor");
    TEST_CHECK(std::abs(dcad_motion::getUnitToSIFactor("linear_velocity", "mm/s") - 0.001) < 1e-12, "linear_velocity mm/s factor");
    TEST_CHECK(dcad_motion::isRateQuantity("angular_velocity"), "angular_velocity is rate");
    TEST_CHECK(dcad_motion::isRateQuantity("linear_velocity"), "linear_velocity is rate");
    TEST_CHECK(!dcad_motion::isRateQuantity("position"), "position is state");
    TEST_CHECK(!dcad_motion::isRateQuantity("angle"), "angle is state");

    std::cout << "[DEBUG] Step 2" << std::endl;
    // 2. Test Client instantiation and signal catalog from active session
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

    // 3. Test Session Attachment & Lifecycle Command Execution (with Deduplication)
    std::cout << "[DEBUG] Step 3 start" << std::endl;

    // Initial state check
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
    std::cout << "[DEBUG] Step 3 complete" << std::endl;

    // 4. Test Live Named Pipe Loopback
    std::cout << "[DEBUG] Step 4 start" << std::endl;
    // Create a mock named pipe server simulating DjehutiCAD
    std::string pipeName = "DjehutiCAD.Motion.Test." + std::to_string(GetCurrentProcessId());
    std::string pipePath = "\\\\.\\pipe\\" + pipeName;

    HANDLE hServerPipe = CreateNamedPipeA(
        pipePath.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        65536,
        65536,
        3000,
        NULL
    );
    TEST_CHECK(hServerPipe != INVALID_HANDLE_VALUE, "Server named pipe created for loopback check");

    if (hServerPipe != INVALID_HANDLE_VALUE)
    {
        // Connect in client via async thread so ConnectNamedPipe on server side does not block
        std::atomic<bool> connectOk { false };
        std::thread clientConnectThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            connectOk = client->connectToPipe(pipePath);
        });

        // Server accepts connection
        BOOL connected = ConnectNamedPipe(hServerPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        TEST_CHECK(connected, "Server accepted pipe connection");

        if (clientConnectThread.joinable())
            clientConnectThread.join();
        TEST_CHECK(connectOk.load(), "Client connected to test pipe");
        std::cout << "[DEBUG] Step 4 pipe connected" << std::endl;

        // Helper to read JUCE frame from server side
        auto serverReadFrame = [hServerPipe](juce::var& out) -> bool {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
            while (std::chrono::steady_clock::now() < deadline)
            {
                DWORD bytesAvail = 0;
                if (PeekNamedPipe(hServerPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail >= 8)
                {
                    uint32_t header[2] = {0, 0};
                    DWORD readHeader = 0;
                    if (!ReadFile(hServerPipe, header, 8, &readHeader, NULL) || readHeader < 8)
                        return false;
                    if (header[0] != dcad_motion::kMagicNumber) return false;
                    uint32_t len = header[1];
                    std::vector<char> buf(len);
                    DWORD totalRead = 0;
                    while (totalRead < len)
                    {
                        DWORD chunk = 0;
                        if (!ReadFile(hServerPipe, buf.data() + totalRead, len - totalRead, &chunk, NULL) || chunk == 0)
                            return false;
                        totalRead += chunk;
                    }
                    out = juce::JSON::parse(juce::String::fromUTF8(buf.data(), (int)len));
                    return out.isObject();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        };

        // Helper to send frame from server side
        auto serverSendFrame = [hServerPipe](const juce::var& payload) {
            auto s = juce::JSON::toString(payload, true);
            juce::MemoryOutputStream m;
            m.writeInt(dcad_motion::kMagicNumber);
            m.writeInt((int)s.getNumBytesAsUTF8());
            m.write(s.toRawUTF8(), s.getNumBytesAsUTF8());
            DWORD written = 0;
            WriteFile(hServerPipe, m.getData(), (DWORD)m.getDataSize(), &written, NULL);
        };

        // 1. Read Hello from DEWB
        juce::var helloMsg;
        TEST_CHECK(serverReadFrame(helloMsg), "Server received Hello frame");
        TEST_CHECK(helloMsg.getProperty("type", {}).toString() == "Hello", "Message is Hello");
        TEST_CHECK(helloMsg.getProperty("role", {}).toString() == "dewb", "Role is dewb");

        // 2. Server answers with Hello and MechanicalInterface
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

        // 3. DEWB should send SignalInterface
        juce::var sigIntMsg;
        TEST_CHECK(serverReadFrame(sigIntMsg), "Server received SignalInterface");
        TEST_CHECK(sigIntMsg.getProperty("type", {}).toString() == "SignalInterface", "Message is SignalInterface");

        // Verify advertised interface contains actual motor signals and no fake carriage/spindle
        const auto* sigArr = sigIntMsg.getProperty("signals", {}).getArray();
        TEST_CHECK(sigArr != nullptr, "Signals array present");
        bool ifaceHasMotorSpeed = false;
        bool ifaceHasCarriage = false;
        if (sigArr)
        {
            for (const auto& s : *sigArr)
            {
                auto id = s.getProperty("id", {}).toString();
                if (id == "motor.speed") ifaceHasMotorSpeed = true;
                if (id == "carriage.position") ifaceHasCarriage = true;
            }
        }
        TEST_CHECK(ifaceHasMotorSpeed, "SignalInterface advertises motor.speed");
        TEST_CHECK(!ifaceHasCarriage, "SignalInterface does not advertise fake carriage");

        // 4A. Server tests unknown signal rejection: proposes binding carriage.position
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

        // DEWB must reject with BindingReject (unknownSignal)
        juce::var rejectMsg;
        TEST_CHECK(serverReadFrame(rejectMsg), "Server received BindingReject for unadvertised signal");
        TEST_CHECK(rejectMsg.getProperty("type", {}).toString() == "BindingReject", "Type is BindingReject");
        const auto* errs = rejectMsg.getProperty("errors", {}).getArray();
        TEST_CHECK(errs != nullptr && !errs->isEmpty(), "BindingReject contains errors list");
        if (errs && !errs->isEmpty())
        {
            TEST_CHECK((*errs)[0].getProperty("code", {}).toString() == "unknownSignal", "Rejection error code is unknownSignal");
        }

        // 4B. Server proposes valid binding for motor.speed
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
        m1->setProperty("initialCoordinate", 0.0);
        mappings.add(juce::var(m1));

        prop->setProperty("mappings", mappings);
        serverSendFrame(juce::var(prop));

        // 5. DEWB must reply with BindingAccept
        juce::var acceptMsg;
        TEST_CHECK(serverReadFrame(acceptMsg), "Server received BindingAccept");
        TEST_CHECK(acceptMsg.getProperty("type", {}).toString() == "BindingAccept", "Type is BindingAccept");
        TEST_CHECK((int)acceptMsg.getProperty("profileRevision", 0) == 2, "ProfileRevision is 2");

        // 6. DEWB sends SynchronizeState
        juce::var syncMsg;
        TEST_CHECK(serverReadFrame(syncMsg), "Server received SynchronizeState");
        TEST_CHECK(syncMsg.getProperty("type", {}).toString() == "SynchronizeState", "Type is SynchronizeState");
        auto seq = syncMsg.getProperty("sequence", {}).toString();
        int initialTimeline = (int)syncMsg.getProperty("timelineGeneration", 0);
        TEST_CHECK(initialTimeline == 1, "Initial timelineGeneration is 1");

        // 7. Server acknowledges with StateApplied
        auto* applied = new juce::DynamicObject();
        applied->setProperty("type", "StateApplied");
        applied->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        applied->setProperty("timelineGeneration", initialTimeline);
        applied->setProperty("profileRevision", 2);
        applied->setProperty("sequence", seq);
        applied->setProperty("duplicate", false);
        serverSendFrame(juce::var(applied));

        // Client should now enter streaming
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        TEST_CHECK(client->getStatus().state == dcad_motion::LinkState::Streaming, "Client transitioned to Streaming after StateApplied");

        // 8. Server sends LifecycleCommand start
        auto* startCmd = new juce::DynamicObject();
        startCmd->setProperty("type", "LifecycleCommand");
        startCmd->setProperty("requestId", "server-req-start-1");
        startCmd->setProperty("modelId", "cad-model-uuid-1234");
        startCmd->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        startCmd->setProperty("profileRevision", 2);
        startCmd->setProperty("timelineGeneration", initialTimeline);
        startCmd->setProperty("command", "start");
        serverSendFrame(juce::var(startCmd));

        juce::var frameOrRes;
        bool gotStartResult = false;
        while (serverReadFrame(frameOrRes))
        {
            if (frameOrRes.getProperty("type", {}).toString() == "LifecycleResult")
            {
                TEST_CHECK(frameOrRes.getProperty("requestId", {}).toString() == "server-req-start-1", "Start RequestId matches");
                TEST_CHECK(frameOrRes.getProperty("outcome", {}).toString() == "applied", "Start outcome applied");
                TEST_CHECK(frameOrRes.getProperty("command", {}).toString() == "start", "Start command start");
                gotStartResult = true;
                break;
            }
        }
        TEST_CHECK(gotStartResult, "Server received confirmed LifecycleResult for start");

        // 9. Server sends LifecycleCommand pause from running
        auto* pauseCmd = new juce::DynamicObject();
        pauseCmd->setProperty("type", "LifecycleCommand");
        pauseCmd->setProperty("requestId", "server-req-pause-1");
        pauseCmd->setProperty("modelId", "cad-model-uuid-1234");
        pauseCmd->setProperty("simulationId", sigIntMsg.getProperty("simulationId", {}));
        pauseCmd->setProperty("profileRevision", 2);
        pauseCmd->setProperty("timelineGeneration", initialTimeline);
        pauseCmd->setProperty("command", "pause");
        serverSendFrame(juce::var(pauseCmd));

        // DEWB must answer with StateFrame + LifecycleResult
        bool gotResult = false;
        while (serverReadFrame(frameOrRes))
        {
            if (frameOrRes.getProperty("type", {}).toString() == "LifecycleResult")
            {
                TEST_CHECK(frameOrRes.getProperty("requestId", {}).toString() == "server-req-pause-1", "RequestId matches");
                TEST_CHECK(frameOrRes.getProperty("outcome", {}).toString() == "applied", "Outcome applied");
                TEST_CHECK(frameOrRes.getProperty("command", {}).toString() == "pause", "Command pause");
                gotResult = true;
                break;
            }
        }
        TEST_CHECK(gotResult, "Server received confirmed LifecycleResult for pause");

        // 10. Test Paused Behavior: verify simulation time and coordinates are stationary
        auto pausedStatus = client->getStatus();
        int64_t pausedTimePs = pausedStatus.simTimePs;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pausedStatus2 = client->getStatus();
        TEST_CHECK(pausedStatus2.simTimePs == pausedTimePs, "Simulation time does not advance while paused");

        // 11. Test Reconnect and State Preservation across Transport Reconnection
        client->disconnect(false);
        CloseHandle(hServerPipe);

        hServerPipe = CreateNamedPipeA(
            pipePath.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            65536,
            65536,
            3000,
            NULL
        );
        TEST_CHECK(hServerPipe != INVALID_HANDLE_VALUE, "Server named pipe recreated for reconnect check");

        std::atomic<bool> reconOk { false };
        std::thread reconnectThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            reconOk = client->connectToPipe(pipePath);
        });

        BOOL reconConnected = ConnectNamedPipe(hServerPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        TEST_CHECK(reconConnected, "Server accepted pipe connection after reconnect");
        if (reconnectThread.joinable())
            reconnectThread.join();
        TEST_CHECK(reconOk.load(), "Client reconnected to pipe");

        // Server receives Hello from reconnected DEWB
        juce::var reconHello;
        TEST_CHECK(serverReadFrame(reconHello), "Server received Hello after reconnect");

        // Server answers with Hello and MechanicalInterface
        serverSendFrame(juce::var(srvHello));
        serverSendFrame(juce::var(srvMech));

        // Server receives SignalInterface
        juce::var reconSigInt;
        TEST_CHECK(serverReadFrame(reconSigInt), "Server received SignalInterface after reconnect");

        // Server re-proposes binding (profileRevision 3)
        auto* reconProp = new juce::DynamicObject();
        reconProp->setProperty("type", "BindingProposal");
        reconProp->setProperty("modelId", "cad-model-uuid-1234");
        reconProp->setProperty("interfaceRevision", 1);
        reconProp->setProperty("profileRevision", 3);
        reconProp->setProperty("simulationId", reconSigInt.getProperty("simulationId", {}));
        reconProp->setProperty("signalInterfaceRevision", (int)reconSigInt.getProperty("signalInterfaceRevision", 1));
        reconProp->setProperty("mappings", mappings);
        serverSendFrame(juce::var(reconProp));

        // DEWB answers with BindingAccept
        juce::var reconAccept;
        TEST_CHECK(serverReadFrame(reconAccept), "Server received BindingAccept after reconnect");

        // DEWB sends SynchronizeState
        juce::var reconSync;
        TEST_CHECK(serverReadFrame(reconSync), "Server received SynchronizeState after reconnect");
        int reconTimeline = (int)reconSync.getProperty("timelineGeneration", 0);
        TEST_CHECK(reconTimeline == initialTimeline, "Timeline generation preserved across reconnect (not reset)");
        int64_t reconSimTime = (int64_t)std::stoll(reconSync.getProperty("simTimePs", "0").toString().toStdString());
        TEST_CHECK(reconSimTime == pausedTimePs, "Simulation time preserved across reconnect (not reset)");

        // Clean disconnect
        client->disconnect(true);
        CloseHandle(hServerPipe);
    }

    std::cout << "\nTest Summary: " << passedChecks << " / " << totalChecks << " checks passed." << std::endl;
    return (passedChecks == totalChecks) ? 0 : 1;
}

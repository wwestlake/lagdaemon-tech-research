#pragma once

// Execution modes (architecture section 03, decision F5). The Runner owns the
// one scheduler thread and drives a Simulation in:
//   free run  - as fast as the computer allows (automated tests, campaigns);
//   paced     - simulated time tracks wall time x factor (1.0 = real time).
//               A late round is never skipped or shortened: the simulation
//               lags, and the lag and overruns are reported;
//   step      - one round, to a time, to the next event, or until a condition.
// Pacing only adds waiting between rounds, so every mode produces the same
// results. Commands from any thread take effect at the next round boundary;
// post() runs a function on the scheduler thread between rounds (the only
// safe way to touch the Simulation while the Runner owns it).

#include "sim_core/Diagnostics.h"
#include "sim_core/Simulation.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sim
{
class Runner
{
public:
    explicit Runner(Simulation& simulation);
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    void runFree(Tick stopAt = kLastSchedulable);
    void runPaced(double factor, Tick stopAt = kLastSchedulable);
    void pause();
    void stepRound();
    void stepToTime(Tick t);
    void stepToNextEvent(Tick limit = kLastSchedulable);
    void stepUntil(std::function<bool(const Simulation&)> condition, Tick limit);
    void post(std::function<void(Simulation&)> action);

    // Blocks until no run or step is active and no command is pending.
    // False on timeout (milliseconds; negative waits forever).
    bool waitIdle(int timeoutMs = -1);

    struct State
    {
        LifecycleState lifecycle = LifecycleState::Created;
        Tick simTime = 0;
        bool running = false;
        double requestedFactor = 0.0; // 0 = free run
        double actualFactor = 0.0;    // simulated seconds per wall second over the current run
        std::int64_t overruns = 0;    // paced rounds that finished later than their wall deadline
        double lagSeconds = 0.0;      // how far behind wall time the last paced round was
        double maxLagSeconds = 0.0;
        std::string lastError;
    };
    State status() const;
    // Wall-clock-dependent diagnostics (overruns, lag). Kept out of the
    // deterministic recording.
    std::vector<Diagnostic> timingDiagnostics() const;

    // A paced round more than this late counts as an overrun (default 1 ms).
    void setOverrunTolerance(std::chrono::microseconds tolerance) { overrunTolerance = tolerance; }

private:
    enum class Mode { Idle, Free, Paced };
    void loop();
    void enqueue(std::function<void()> command);
    void ensureRunning();
    void finishRun();
    void startRun(Mode mode, double factor, Tick stopAt);
    void reportOverrun(double lagSeconds);

    Simulation& sim;
    std::thread thread;
    mutable std::mutex mutex;
    std::condition_variable wake, idle;
    std::deque<std::function<void()>> commands;
    bool quit = false, busy = false;

    Mode mode = Mode::Idle;
    double factor = 1.0;
    Tick stopAt = kLastSchedulable;
    Tick runStartSim = 0;
    std::chrono::steady_clock::time_point runStartWall;
    std::chrono::microseconds overrunTolerance { 1000 };

    State state;
    std::vector<Diagnostic> timing;
};
}

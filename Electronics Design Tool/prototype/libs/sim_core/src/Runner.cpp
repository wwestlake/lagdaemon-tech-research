#include "sim_core/Runner.h"

#include <algorithm>
#include <cstdio>

namespace sim
{
Runner::Runner(Simulation& simulation) : sim(simulation)
{
    state.lifecycle = sim.state();
    state.simTime = sim.now().tick;
    thread = std::thread([this] { loop(); });
}

Runner::~Runner()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable())
        thread.join();
}

void Runner::enqueue(std::function<void()> command)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        commands.push_back(std::move(command));
    }
    wake.notify_all();
}

void Runner::ensureRunning()
{
    if (sim.state() == LifecycleState::Initialized || sim.state() == LifecycleState::Paused)
        sim.start();
}

void Runner::finishRun()
{
    std::lock_guard<std::mutex> lock(mutex);
    mode = Mode::Idle;
    if (sim.state() == LifecycleState::Running)
        sim.pause();
    state.running = false;
    state.lifecycle = sim.state();
    state.simTime = sim.now().tick;
    if (sim.state() == LifecycleState::Faulted)
        state.lastError = sim.lastError();
}

void Runner::startRun(Mode m, double f, Tick stop)
{
    ensureRunning();
    std::lock_guard<std::mutex> lock(mutex);
    if (sim.state() != LifecycleState::Running)
    {
        state.lastError = sim.lastError();
        return;
    }
    mode = m;
    factor = f;
    stopAt = stop;
    runStartSim = sim.now().tick;
    runStartWall = std::chrono::steady_clock::now();
    state.running = true;
    state.requestedFactor = m == Mode::Paced ? f : 0.0;
    state.actualFactor = 0.0;
    state.overruns = 0;
    state.lagSeconds = 0.0;
    state.maxLagSeconds = 0.0;
}

void Runner::runFree(Tick stop) { enqueue([this, stop] { startRun(Mode::Free, 0.0, stop); }); }

void Runner::runPaced(double f, Tick stop)
{
    enqueue([this, f, stop] { startRun(Mode::Paced, f > 0.0 ? f : 1.0, stop); });
}

void Runner::pause() { enqueue([this] { finishRun(); }); }

void Runner::stepRound()
{
    enqueue([this] {
        ensureRunning();
        sim.advanceRound();
        finishRun();
    });
}

void Runner::stepToTime(Tick t)
{
    enqueue([this, t] {
        ensureRunning();
        sim.runUntil(t);
        finishRun();
    });
}

void Runner::stepToNextEvent(Tick limit)
{
    enqueue([this, limit] {
        ensureRunning();
        sim.runToNextEvent(limit);
        finishRun();
    });
}

void Runner::stepUntil(std::function<bool(const Simulation&)> condition, Tick limit)
{
    enqueue([this, condition = std::move(condition), limit] {
        ensureRunning();
        sim.runUntilCondition(condition, limit);
        finishRun();
    });
}

void Runner::post(std::function<void(Simulation&)> action)
{
    enqueue([this, action = std::move(action)] { action(sim); });
}

bool Runner::waitIdle(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex);
    auto done = [this] { return commands.empty() && !busy && mode == Mode::Idle; };
    if (timeoutMs < 0)
    {
        idle.wait(lock, done);
        return true;
    }
    return idle.wait_for(lock, std::chrono::milliseconds(timeoutMs), done);
}

Runner::State Runner::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}

std::vector<Diagnostic> Runner::timingDiagnostics() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return timing;
}

void Runner::reportOverrun(double lag)
{
    std::lock_guard<std::mutex> lock(mutex);
    ++state.overruns;
    state.lagSeconds = lag;
    state.maxLagSeconds = std::max(state.maxLagSeconds, lag);
    // The first overrun of a run, then every 100th: visible without flooding.
    if (state.overruns == 1 || state.overruns % 100 == 0)
    {
        Diagnostic d;
        d.time = sim.now();
        d.severity = Severity::Warning;
        d.category = Category::Timing;
        d.wallClockDependent = true;
        char text[200];
        std::snprintf(text, sizeof text, "paced run behind wall time by %.3f ms (overrun %lld; requested %.3gx real time).",
                      lag * 1e3, (long long)state.overruns, factor);
        d.message = text;
        timing.push_back(d);
    }
}

void Runner::loop()
{
    for (;;)
    {
        std::function<void()> command;
        Mode current;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this] { return quit || !commands.empty() || mode != Mode::Idle; });
            if (quit)
                break;
            if (!commands.empty())
            {
                command = std::move(commands.front());
                commands.pop_front();
            }
            busy = true;
            current = mode;
        }
        if (command)
            command();
        else if (current != Mode::Idle)
        {
            const auto next = sim.peekNextRoundTime();
            if (sim.state() != LifecycleState::Running || next == kNever || next > stopAt)
                finishRun();
            else
            {
                const auto out = sim.advanceRound();
                if (out.kind != RoundOutcome::Kind::Advanced)
                    finishRun();
                else
                {
                    const auto now = std::chrono::steady_clock::now();
                    const double simElapsed = ticksToSeconds(sim.now().tick - runStartSim);
                    const double wallElapsed = std::chrono::duration<double>(now - runStartWall).count();
                    if (current == Mode::Paced)
                    {
                        const auto target = runStartWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                               std::chrono::duration<double>(simElapsed / factor));
                        if (now > target + overrunTolerance)
                            reportOverrun(std::chrono::duration<double>(now - target).count());
                        else if (now < target)
                        {
                            // Wait for wall time, but wake at once for a command.
                            std::unique_lock<std::mutex> lock(mutex);
                            wake.wait_until(lock, target, [this] { return quit || !commands.empty(); });
                        }
                    }
                    std::lock_guard<std::mutex> lock(mutex);
                    state.simTime = sim.now().tick;
                    state.lifecycle = sim.state();
                    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStartWall).count();
                    state.actualFactor = wall > 0.0 ? simElapsed / wall : 0.0;
                    (void)wallElapsed;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            busy = false;
            state.lifecycle = sim.state();
            state.simTime = sim.now().tick;
        }
        idle.notify_all();
    }
}
}

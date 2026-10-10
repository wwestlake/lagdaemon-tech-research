// The FRust debugger (FrustExecution) without a window: real node programs
// compiled by the node compiler and run by the embedded FRust compiler on the
// executor's worker thread. This thread plays the message thread: it pumps
// what the executor posts and never waits on the program.

#include <JuceHeader.h>

#include "../../Source/FrustDebuggerPanel.h"
#include "../../Source/FrustEngine.h"
#include "../../Source/FrustExecution.h"
#include "../../Source/NodeDesignerPanel.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

extern "C" void (*g_frust_dbg_callback)(int, int);

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const juce::String& name, const juce::String& detail = {})
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), ok || detail.isEmpty() ? "" : ("  -- " + detail).toRawUTF8());
    std::fflush(stdout);
}

// The "message thread" queue: the executor posts here, this thread pumps.
std::mutex queueMutex;
std::deque<std::function<void()>> queue;
std::atomic<int> delivered { 0 };

void pump()
{
    for (;;)
    {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (queue.empty()) return;
            fn = std::move(queue.front());
            queue.pop_front();
        }
        fn();
        ++delivered;
    }
}

frust_exec::Executor& executor() { return frust_exec::Executor::instance(); }

// Pumps until the state satisfies `until` (or the time is up). Never blocks
// for more than a millisecond at a time.
bool waitFor(const std::function<bool(const frust_exec::Snapshot&)>& until, int timeoutMs = 20000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end)
    {
        pump();
        if (until(executor().snapshot()))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    pump();
    return until(executor().snapshot());
}

bool settled(const frust_exec::Snapshot& s) { return s.state == frust_exec::State::Paused || !frust_exec::isActive(s.state); }
bool ended(const frust_exec::Snapshot& s) { return !frust_exec::isActive(s.state); }

juce::String describe(const frust_exec::Snapshot& s)
{
    return juce::JSON::toString(frust_exec::toVar(s), true);
}

bool command(frust_exec::Command c)
{
    std::string error;
    const bool ok = executor().command(c, error);
    if (!ok) std::printf("   command %s refused: %s\n", frust_exec::commandName(c), error.c_str());
    return ok && waitFor(settled);
}

const frust_exec::Variable* variable(const frust_exec::Frame& f, const std::string& name)
{
    for (const auto& v : f.variables)
        if (v.name == name) return &v;
    return nullptr;
}

const frust_exec::Watch* watch(const frust_exec::Snapshot& s, const std::string& node)
{
    for (const auto& w : s.watches)
        if (w.nodeId == node) return &w;
    return nullptr;
}

juce::File writeTemp(const juce::String& name, const juce::String& text)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("frust_debugger_tests");
    dir.createDirectory();
    auto f = dir.getChildFile(name);
    f.replaceWithText(text);
    return f;
}

bool startDebug(NodeDesignerPanel& panel, juce::String& error)
{
    frust_exec::Program program;
    if (!panel.buildDebugProgram(program, error))
        return false;
    std::string startError;
    if (!executor().start(std::move(program), startError))
    {
        error = juce::String(startError);
        return false;
    }
    return true;
}

void syncMarkers(NodeDesignerPanel& panel)
{
    std::vector<frust_exec::Breakpoint> bps;
    for (const auto& m : panel.breakpointMarkers())
        bps.push_back({ m.nodeId.toStdString(), 0, m.enabled, 0 });
    std::vector<std::string> watches;
    for (const auto& w : panel.watchedNodes())
        watches.push_back(w.toStdString());
    executor().updateMarkers(panel.programId().toStdString(), bps, watches);
}

// An executable program: Start -> Print "first" -> Print 42 -> Print "third" -> End.
void buildPrints(NodeDesignerPanel& panel)
{
    juce::String e;
    panel.newGraph("node_graph", false);
    panel.addNodeOfType("event_start", 0, 0, "start", e);
    panel.addNodeOfType("literal_string", 0, 150, "first_text", e);
    panel.addNodeOfType("print", 250, 0, "say_first", e);
    panel.addNodeOfType("literal_i64", 250, 150, "answer", e);
    panel.addNodeOfType("print", 500, 0, "say_answer", e);
    panel.addNodeOfType("literal_string", 500, 150, "third_text", e);
    panel.addNodeOfType("print", 750, 0, "say_third", e);
    panel.addNodeOfType("end", 1000, 0, "done", e);
    panel.connect("start", "start", "say_first", "in", e);
    panel.connect("first_text", "value", "say_first", "value", e);
    panel.connect("say_first", "then", "say_answer", "in", e);
    panel.connect("answer", "value", "say_answer", "value", e);
    panel.connect("say_answer", "then", "say_third", "in", e);
    panel.connect("third_text", "value", "say_third", "value", e);
    panel.connect("say_third", "then", "done", "in", e);
    panel.setNodeParameter("first_text", "text", "first", e);
    panel.setNodeParameter("answer", "value", "42", e);
    panel.setNodeParameter("third_text", "text", "third", e);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::String error, message;

    std::printf("-- which frust_dbg_tick the JIT calls --\n");
    {
        // Before the executor installs its handler in both places: count the
        // app's registered djehuti_frust_dbg_tick and the runtime's export
        // (through g_frust_dbg_callback) separately.
        static std::atomic<int> viaRegistration { 0 }, viaRuntime { 0 };
        frust_engine::setTickHandler([](int, int) { ++viaRegistration; });
        g_frust_dbg_callback = [](int, int) { ++viaRuntime; };
        const auto r = frust_engine::runScript("pub fn run() -> String = {\n    let a: i64 = 1 + 2;\n    \"ok\"\n}\n");
        std::printf("   ticks: runtime export %d, app registration %d\n", viaRuntime.load(), viaRegistration.load());
        check(r.ok && viaRuntime.load() > 0 && viaRegistration.load() == 0,
              "compiled code calls the FRust runtime's exported frust_dbg_tick (g_frust_dbg_callback), not the app's registration",
              "runtime " + juce::String(viaRuntime.load()) + ", registration " + juce::String(viaRegistration.load()) + " " + juce::String(r.report()));
        frust_engine::setTickHandler(nullptr);
        g_frust_dbg_callback = nullptr;
    }

    executor().setPoster([](std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push_back(std::move(fn));
    });

    std::printf("-- plain run on the worker --\n");
    {
        NodeDesignerPanel panel;
        buildPrints(panel);
        juce::String script;
        check(panel.compileProgram(message) && panel.buildRunScript(script, error), "the program compiles", message + error);
        frust_exec::Program program;
        program.label = "prints";
        program.programId = "prints";
        program.script = script.toStdString();
        std::string startError;
        check(executor().start(program, startError), "Run starts on the worker", juce::String(startError));
        check(waitFor(ended) && executor().snapshot().state == frust_exec::State::Completed, "it completes", describe(executor().snapshot()));
        const auto out = juce::String(executor().snapshot().result.output);
        check(out.indexOf("first") >= 0 && out.indexOf("first") < out.indexOf("42") && out.indexOf("42") < out.indexOf("third"),
              "its output is all three prints in order", out);
    }

    std::printf("-- breakpoints in an executable program --\n");
    {
        NodeDesignerPanel panel;
        buildPrints(panel);
        panel.setBreakpoint("say_answer", true, true, error);
        panel.setWatch("say_first", true, error);
        panel.setWatch("answer", true, error);
        check(startDebug(panel, error), "Start Debugging", error);
        check(waitFor(settled), "it stops", describe(executor().snapshot()));
        auto s = executor().snapshot();
        check(s.state == frust_exec::State::Paused && s.pauseReason == "breakpoint" && s.nodeId == "say_answer",
              "paused at the breakpoint on the say_answer node", describe(s));
        check(s.stack.size() == 1 && s.stack[0].function == "main", "the stack is main()", describe(s));
        const auto* first = s.stack.empty() ? nullptr : variable(s.stack[0], "first_text");
        check(first != nullptr && first->value == "\"first\"", "main's variables hold the value already printed", describe(s));
        const auto* w1 = watch(s, "say_first");
        const auto* w2 = watch(s, "answer");
        check(w1 != nullptr && w1->available && w1->value == "\"first\"", "a watch on the first Print shows what it printed", describe(s));
        check(w2 != nullptr && !w2->available && w2->hasValue, "a watch on 42 is not computed yet (its line has not run)", describe(s));

        // The message thread stays free while the program is paused.
        const auto t0 = std::chrono::steady_clock::now();
        int reads = 0;
        for (int i = 0; i < 200; ++i)
        {
            pump();
            if (executor().snapshot().state == frust_exec::State::Paused) ++reads;
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(reads == 200 && ms < 1000.0, "while paused, 200 message-thread rounds run in " + juce::String(ms, 1) + " ms");

        check(command(frust_exec::Command::Continue) && executor().snapshot().state == frust_exec::State::Completed,
              "Continue runs to the end", describe(executor().snapshot()));
        s = executor().snapshot();
        check(juce::String(s.result.output).contains("third"), "and the rest of the program ran", juce::String(s.result.output));
        check(watch(s, "answer") != nullptr && watch(s, "answer")->value == "42", "the watch on 42 now shows 42", describe(s));
    }

    std::printf("-- several breakpoints, a disabled one, once per line --\n");
    {
        NodeDesignerPanel panel;
        buildPrints(panel);
        panel.setBreakpoint("say_first", true, true, error);
        panel.setBreakpoint("say_answer", true, false, error); // disabled
        panel.setBreakpoint("say_third", true, true, error);
        check(startDebug(panel, error) && waitFor(settled) && executor().snapshot().nodeId == "say_first",
              "stops at the first breakpoint", describe(executor().snapshot()));
        check(command(frust_exec::Command::Continue) && executor().snapshot().nodeId == "say_third",
              "Continue skips the disabled breakpoint (and does not stop again on the same line) and stops at the third",
              describe(executor().snapshot()));
        auto s = executor().snapshot();
        int firstHits = 0, thirdHits = 0, answerHits = 0;
        for (const auto& b : s.breakpoints)
        {
            if (b.nodeId == "say_first") firstHits = b.hits;
            if (b.nodeId == "say_third") thirdHits = b.hits;
            if (b.nodeId == "say_answer") answerHits = b.hits;
        }
        check(firstHits == 1 && thirdHits == 1 && answerHits == 0, "hit counts: 1, 0 (disabled), 1", describe(s));
        check(command(frust_exec::Command::Continue) && executor().snapshot().state == frust_exec::State::Completed, "then it completes");
    }

    std::printf("-- a function graph: variables, watches, recompiling --\n");
    {
        const auto lib = R"({"schemaVersion": 2, "name": "Sum", "diagramType": "node_graph",
            "targetOptions": {"frust": {"functionName": "compute", "projectType": "lib"}},
            "functionName": "compute", "params": [], "output": "sum",
            "nodes": [{"id": "ten", "type": "literal_i64", "value": 10, "x": 0, "y": 0},
                      {"id": "two", "type": "literal_i64", "value": 2, "x": 0, "y": 100},
                      {"id": "sum", "type": "add", "x": 200, "y": 50, "inputs": [{"ref": "ten"}, {"ref": "two"}]}]})";
        NodeDesignerPanel panel;
        juce::String problems;
        panel.openFile(writeTemp("sum.frnode.json", lib), problems);
        panel.setBreakpoint("sum", true, true, error);
        panel.setWatch("sum", true, error);
        check(startDebug(panel, error) && waitFor(settled), "debugging compute() stops", error + describe(executor().snapshot()));
        auto s = executor().snapshot();
        const int firstLine = s.line;
        check(s.nodeId == "sum" && !s.stack.empty() && s.stack[0].function == "compute", "at the sum node in compute()", describe(s));
        const auto* ten = s.stack.empty() ? nullptr : variable(s.stack[0], "ten");
        const auto* two = s.stack.empty() ? nullptr : variable(s.stack[0], "two");
        check(ten != nullptr && ten->value == "10" && two != nullptr && two->value == "2" && variable(s.stack[0], "sum") == nullptr,
              "variables: ten = 10, two = 2, sum not yet", describe(s));
        check(watch(s, "sum") != nullptr && !watch(s, "sum")->available, "the watch on sum: not computed yet", describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over");
        s = executor().snapshot();
        check(s.state == frust_exec::State::Paused && s.line == firstLine + 1 && s.stack.size() == 1, "stops on the next line of compute()", describe(s));
        check(watch(s, "sum") != nullptr && watch(s, "sum")->value == "12", "the watch on sum shows 12", describe(s));
        check(command(frust_exec::Command::Continue) && juce::String(executor().snapshot().result.output).trim() == "12",
              "Continue: compute() returns 12", describe(executor().snapshot()));

        // Recompiling after an edit: the breakpoint follows the node to its new line.
        panel.addNodeOfType("literal_i64", 0, 200, "three", error);
        panel.addNodeOfType("literal_i64", 0, 250, "four", error);
        check(startDebug(panel, error) && waitFor(settled), "debugging the edited program stops", error);
        s = executor().snapshot();
        check(s.nodeId == "sum" && s.line != firstLine, "the breakpoint is at sum's new line (" + juce::String(s.line) + ", was " + juce::String(firstLine) + ")",
              describe(s));
        std::string stopError;
        executor().stop(stopError);
        waitFor(ended);
    }

    std::printf("-- a state machine: Step Into, Over, Out --\n");
    {
        NodeDesignerPanel panel;
        panel.newGraph("state_machine", true);
        // The initial state's node: its function is called by initial_state()
        // (and again by state_name and step).
        const auto graph = panel.describeGraph();
        juce::String initialNode, transitionNode;
        if (auto* list = graph.getProperty("nodes", {}).getArray())
            for (const auto& n : *list)
            {
                if (n.getProperty("type", {}).toString() == "sm_state" && (bool)n.getProperty("parameters", {}).getProperty("initial", false))
                    initialNode = n.getProperty("id", {}).toString();
                if (n.getProperty("type", {}).toString() == "sm_transition" && transitionNode.isEmpty())
                    transitionNode = n.getProperty("id", {}).toString();
            }
        check(initialNode.isNotEmpty() && transitionNode.isNotEmpty(), "the starter machine has an initial state and a transition",
              juce::JSON::toString(graph));
        panel.setBreakpoint(initialNode, true, true, error);
        check(startDebug(panel, error) && waitFor(settled), "debugging the state machine stops", error);
        auto s = executor().snapshot();
        auto functions = [](const frust_exec::Snapshot& snap) {
            juce::StringArray names;
            for (const auto& f : snap.stack) names.add(f.function);
            return names.joinIntoString(" < ");
        };
        check(s.nodeId == initialNode && s.stack.size() == 3 && s.stack[1].function == "initial_state" && s.stack[2].function == "main",
              "in the initial state's function, called from initial_state(), called from main(): " + functions(s), describe(s));

        // The initial state's function is called again later; disable the
        // breakpoint (the running session follows) so stepping is not interrupted.
        panel.setBreakpoint(initialNode, true, false, error);
        syncMarkers(panel);

        check(command(frust_exec::Command::StepOut), "Step Out");
        s = executor().snapshot();
        check(s.stack.size() == 2 && s.stack[0].function == "initial_state", "back in initial_state(): " + functions(s), describe(s));
        check(command(frust_exec::Command::StepOut), "Step Out again");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.stack[0].function == "main", "back in main(): " + functions(s), describe(s));
        const int mainLine = s.line;

        check(command(frust_exec::Command::StepOver), "Step Over");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.line == mainLine + 1, "the next line of main()", describe(s));
        const auto* state = s.stack.empty() ? nullptr : variable(s.stack[0], "state");
        check(state != nullptr && juce::String(state->value).startsWith("0 ("), "main's state variable: " + (state ? juce::String(state->value) : juce::String("none")),
              describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over (prints the state's name)");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.line == mainLine + 2, "over the line that calls state_name(): still main()", describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over (to the step() line)");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.line == mainLine + 3 && variable(s.stack[0], "after") == nullptr,
              "at the line that calls step(); after is not recorded until that line has run", describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over (the step() call)");
        s = executor().snapshot();
        const auto afterVar = s.stack.empty() || variable(s.stack[0], "after") == nullptr ? frust_exec::Variable {} : *variable(s.stack[0], "after");
        const auto* after = &afterVar;
        check(s.stack.size() == 1 && s.line == mainLine + 4 && afterVar.value == "1 (Listening)",
              "over the step() call: still main(), after = " + juce::String(afterVar.value), describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over (a print with no calls)");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.line == mainLine + 5, "the line that calls state_name(after)", describe(s));
        check(command(frust_exec::Command::StepInto), "Step Into the line that calls state_name(after)");
        s = executor().snapshot();
        check(s.stack.size() == 2 && s.stack[0].function == "state_name", "inside state_name(): " + functions(s), describe(s));
        const auto* arg = s.stack.empty() ? nullptr : variable(s.stack[0], "state");
        check(arg != nullptr && arg->value == after->value, "its argument is main's after value", describe(s));
        check(command(frust_exec::Command::StepOut), "Step Out of state_name()");
        s = executor().snapshot();
        check(s.stack.size() == 1 && s.stack[0].function == "main", "back in main()", describe(s));
        check(command(frust_exec::Command::Continue) && executor().snapshot().state == frust_exec::State::Completed, "Continue to the end",
              describe(executor().snapshot()));
        check(juce::String(executor().snapshot().result.output).contains("After first transition event:"), "the machine's output is intact",
              juce::String(executor().snapshot().result.output));

        // A breakpoint on the transition: stops inside step() when it fires.
        panel.setBreakpoint(transitionNode, true, true, error);
        check(startDebug(panel, error) && waitFor(settled), "debugging with a transition breakpoint stops", error);
        s = executor().snapshot();
        check(s.nodeId == transitionNode && !s.stack.empty() && s.stack[0].function == "step", "at the transition, in step(): " + functions(s), describe(s));
        const auto* current = s.stack.empty() ? nullptr : variable(s.stack[0], "current_state");
        check(current != nullptr && current->value == "0 (Idle)", "step's current_state = 0 (Idle)", describe(s));
        check(command(frust_exec::Command::StepOver), "Step Over the transition");
        s = executor().snapshot();
        const auto* next = s.stack.empty() ? nullptr : variable(s.stack[0], "next_state");
        check(next != nullptr && next->value == "1 (Listening)", "step's next_state = 1 (Listening)", describe(s));
        std::string stopError;
        executor().stop(stopError);
        check(waitFor(ended), "stopped");
    }

    std::printf("-- Stop and conflicts --\n");
    {
        NodeDesignerPanel panel;
        buildPrints(panel);
        panel.setBreakpoint("say_answer", true, true, error);
        check(startDebug(panel, error) && waitFor(settled) && executor().snapshot().state == frust_exec::State::Paused, "paused at a breakpoint");
        frust_exec::Program other;
        other.label = "other";
        other.script = "pub fn run() -> String = { \"x\" }\n";
        std::string startError;
        check(!executor().start(other, startError), "starting another program while one is paused is refused", juce::String(startError));
        std::string stopError;
        check(executor().stop(stopError), "Stop", juce::String(stopError));
        check(waitFor(ended, 5000) && executor().snapshot().state == frust_exec::State::Cancelled, "the paused program is released and cancelled",
              describe(executor().snapshot()));
        check(juce::String(executor().snapshot().result.output).contains("first") && !juce::String(executor().snapshot().result.output).contains("third"),
              "what it printed before the stop is kept", juce::String(executor().snapshot().result.output));
        check(executor().start(other, startError) && waitFor(ended) && executor().snapshot().result.output == "x",
              "the next program starts and runs", juce::String(startError) + describe(executor().snapshot()));
    }

    std::printf("-- Stop while running, Pause --\n");
    {
        // A long loop (no debug compile): Stop ends it at its next expression.
        frust_exec::Program loop;
        loop.label = "loop";
        loop.script = "pub fn run() -> String = {\n    print_line(\"started\");\n    let mut i: i64 = 0;\n"
                      "    while i < 4000000000000 { i = i + 1 };\n    \"finished\"\n}\n";
        std::string startError;
        check(executor().start(loop, startError) && waitFor([](const frust_exec::Snapshot& s) { return s.state == frust_exec::State::Running; }),
              "a long loop is running", juce::String(startError));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(executor().snapshot().state == frust_exec::State::Running, "still running after 300 ms");
        std::string stopError;
        const auto t0 = std::chrono::steady_clock::now();
        check(executor().stop(stopError), "Stop", juce::String(stopError));
        check(waitFor(ended, 5000) && executor().snapshot().state == frust_exec::State::Cancelled, "it is cancelled",
              describe(executor().snapshot()));
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(ms < 2000.0, "within " + juce::String(ms, 1) + " ms");
        check(juce::String(executor().snapshot().result.output).contains("started"), "its output so far is kept",
              juce::String(executor().snapshot().result.output));
        const auto after = frust_engine::runScript("pub fn run() -> String = {\n    print_line(\"after\");\n    \"ok\"\n}\n");
        check(after.ok && juce::String(after.output).contains("after"), "output capture works for the next run (standard output restored)",
              juce::String(after.report()) + juce::String(after.output));

        // Pause: a debug-compiled loop (the debugger's calls written by hand).
        frust_exec::Program spin;
        spin.label = "spin";
        spin.programId = "spin";
        spin.script = "pub fn spin() -> i64 = { djehuti_dbg_enter(0);\n"
                      "    let mut i: i64 = 0;\n"
                      "    while i < 4000000000000 {\n"
                      "        i = i + 1\n"
                      "    };\n"
                      "    djehuti_dbg_leave(0); i\n"
                      "}\n"
                      "pub fn run() -> String = { spin(); \"x\" }\n";
        spin.programLines = 7;
        spin.debug = true;
        spin.prelude = node_compiler::DebugPrelude();
        spin.info.functions.push_back({ "spin", "" });
        check(executor().start(spin, startError) && waitFor([](const frust_exec::Snapshot& s) { return s.state == frust_exec::State::Running; }),
              "a debug session is running", juce::String(startError));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::string pauseError;
        check(executor().pause(pauseError) && waitFor(settled, 5000), "Pause", juce::String(pauseError));
        auto s = executor().snapshot();
        check(s.state == frust_exec::State::Paused && s.pauseReason == "pause" && !s.stack.empty() && s.stack[0].function == "spin"
                  && (s.line == 3 || s.line == 4),
              "paused in the loop, in spin()", describe(s));
        const int pausedLine = s.line;
        check(command(frust_exec::Command::StepInto) && executor().snapshot().state == frust_exec::State::Paused
                  && executor().snapshot().line == (pausedLine == 3 ? 4 : 3),
              "Step Into stops at the loop's other line", describe(executor().snapshot()));
        executor().stop(stopError);
        check(waitFor(ended, 5000) && executor().snapshot().state == frust_exec::State::Cancelled, "Stop ends it");
    }

    std::printf("-- panels closed during execution --\n");
    {
        // The debugger panel and the node editor are destroyed while a program
        // is paused; what the executor posts afterwards reaches nothing destroyed.
        auto panel = std::make_unique<NodeDesignerPanel>();
        buildPrints(*panel);
        panel->setBreakpoint("say_answer", true, true, error);
        FrustDebuggerPanel::Actions actions;
        auto debugger = std::make_unique<FrustDebuggerPanel>(std::move(actions));
        check(startDebug(*panel, error) && waitFor(settled), "paused, with a debugger panel open");
        debugger.reset();
        panel.reset();
        std::string stopError;
        executor().stop(stopError);
        check(waitFor(ended, 5000) && executor().snapshot().state == frust_exec::State::Cancelled,
              "after the panels are gone the program is stopped and its updates go nowhere");
    }

    std::printf("-- ticks on other threads --\n");
    {
        // A FRust program run directly on another thread (as the audio DSP
        // code is) is not affected by the debugger.
        frust_engine::Result result;
        std::thread other([&result] { result = frust_engine::runScript("pub fn run() -> String = { \"plain\" }\n"); });
        other.join();
        check(result.ok && result.output == "plain", "a FRust program on another thread runs normally", juce::String(result.report()));
    }

    pump();
    std::printf("%d checks, %s\n", checks, failures == 0 ? "ALL PASSED" : (juce::String(failures) + " FAILED").toRawUTF8());
    return failures == 0 ? 0 : 1;
}

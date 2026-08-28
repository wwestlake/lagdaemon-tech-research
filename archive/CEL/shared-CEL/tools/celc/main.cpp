#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core_components.h"
#include "engine/script_component.h"
#include "engine/simulation.h"
#include "engine/world.h"
#include "lang/ast_printer.h"
#include "lang/compiler.h"
#include "lang/diagnostics.h"
#include "lang/jit/runtime.h"
#include "lang/jit/script_runtime.h"
#include "lang/nodegen/graph_to_source.h"
#include "lang/nodegen/node_catalog.h"
#include "lang/sema.h"
#include "node_system/celg_serialization.h"
#include "nodegen_fixtures.h"
#include "selftest_graph.h"

namespace {

int RunDumpAst(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }

    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);

    diagnostics.PrintAll(std::cerr);

    if (program == nullptr || diagnostics.HasErrors()) {
        return 1;
    }

    ce::lang::PrintAst(*program, std::cout);
    return 0;
}

// GS-Interop: parses a comma-separated domain list ("core,world") into
// an IntrinsicDomainSet -- celc's own CLI vehicle for exercising the
// cross-app capability-gating mechanism (see lang/type.h's
// IntrinsicDomain and docs/CROSS_APP_LANGUAGE_DOMAINS.md) from a real
// compile, not just from unit-test code. An empty/absent flag means "no
// restriction," matching IntrinsicDomainSet::All()'s role as the
// default everywhere else. Prints an error and returns false for an
// unknown domain name rather than silently ignoring it.
bool ParseDomainSet(const std::string& csv, ce::lang::IntrinsicDomainSet& outSet) {
    std::vector<ce::lang::IntrinsicDomain> domains;
    std::istringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token == "core") {
            domains.push_back(ce::lang::IntrinsicDomain::Core);
        } else if (token == "world") {
            domains.push_back(ce::lang::IntrinsicDomain::World);
        } else {
            std::cerr << "celc: unknown domain '" << token << "' (expected 'core' or 'world')" << std::endl;
            return false;
        }
    }
    outSet = ce::lang::IntrinsicDomainSet::Only(domains);
    return true;
}

// --check: parse + semantic analysis, no codegen (that's GS4). Kept as
// its own subcommand rather than folded into --dump-ast, since
// --dump-ast's output is diffed against fixtures written before sema
// existed -- running sema there too would mean every parse fixture also
// has to be a well-typed program, which isn't the property those
// fixtures are testing.
//
// `domains` (GS-Interop, optional, "" = no restriction/All()) makes the
// cross-app capability-gating mechanism exercisable from the CLI, e.g.
// `celc --check script.cel --domains core` to compile as if this were a
// host with no World access.
int RunCheck(const std::string& path, const std::string& domains) {
    ce::lang::IntrinsicDomainSet allowedDomains = ce::lang::IntrinsicDomainSet::All();
    if (!domains.empty() && !ParseDomainSet(domains, allowedDomains)) {
        return 1;
    }

    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }

    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);
    if (program == nullptr || diagnostics.HasErrors()) {
        diagnostics.PrintAll(std::cerr);
        return 1;
    }

    const bool ok = ce::lang::AnalyzeProgram(*program, diagnostics, allowedDomains);
    diagnostics.PrintAll(std::cerr);
    if (!ok) {
        return 1;
    }

    std::cout << "OK" << std::endl;
    return 0;
}

// Parses+checks `path`, returning the resulting Program (owned by
// `arena`, which callers must keep alive for as long as they use the
// result), or nullptr with diagnostics already printed on failure.
// Shared by --run and --emit-llvm, which both need a fully-checked
// program before they can touch codegen.
ce::lang::Program* ParseAndCheck(const std::string& path, ce::lang::AstArena& arena,
                                  ce::lang::DiagnosticEngine& diagnostics) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return nullptr;
    }
    ce::lang::Program* program = ce::lang::ParseProgram(file, arena, diagnostics);
    if (program == nullptr || diagnostics.HasErrors()) {
        diagnostics.PrintAll(std::cerr);
        return nullptr;
    }
    if (!ce::lang::AnalyzeProgram(*program, diagnostics)) {
        diagnostics.PrintAll(std::cerr);
        return nullptr;
    }
    return program;
}

// --run <file.cel> [--entry NAME] [--opt N]: compiles to LLVM IR,
// optimizes at level N (default 2), JITs it, and calls the
// zero-argument function `NAME` (default "main"), printing its result.
// GS4's exec tests run every fixture at both -O0 and -O2 (see
// Language/tests/run_exec_test.cmake) specifically to check the
// optimizer didn't change program *behavior*, not just performance.
int RunExec(const std::string& path, const std::string& entryPoint, int optLevel) {
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ParseAndCheck(path, arena, diagnostics);
    if (program == nullptr) {
        return 1;
    }

    ce::lang::jit::Runtime runtime;
    const ce::lang::jit::ExecResult result = runtime.CompileAndRun(*program, entryPoint, optLevel);

    switch (result.kind) {
        case ce::lang::jit::ResultKind::Int:
            std::cout << result.intValue << std::endl;
            break;
        case ce::lang::jit::ResultKind::Float:
            std::cout << result.floatValue << std::endl;
            break;
        case ce::lang::jit::ResultKind::Bool:
            std::cout << (result.boolValue ? "true" : "false") << std::endl;
            break;
        case ce::lang::jit::ResultKind::Void:
            std::cout << "(void)" << std::endl;
            break;
        case ce::lang::jit::ResultKind::Error:
            std::cerr << "celc: " << result.errorMessage << std::endl;
            return 1;
    }

    if (result.faulted) {
        std::cerr << "celc: script faulted: " << result.faultMessage << std::endl;
        return 1;
    }
    return 0;
}

// --run-world <file.cel> [--entry NAME] [--ticks N] [--dt D] [--opt N]:
// like --run, but backed by a real, internally-constructed
// ce::engine::World/ScriptContext (Runtime::RunWorldProgram) that
// persists across `ticks` calls to the zero-argument function `NAME`
// (default "tick") -- so a script that spawns entities or moves them
// via set_position on one tick still sees that state on the next. This
// is GS5's headless verification path for World-touching intrinsics
// (get/set_position, spawn, ...), since celc has no engine editor/server
// around it.
int RunWorld(const std::string& path, const std::string& entryPoint, int ticks, float dt, int optLevel) {
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ParseAndCheck(path, arena, diagnostics);
    if (program == nullptr) {
        return 1;
    }

    ce::lang::jit::Runtime runtime;
    const ce::lang::jit::ExecResult result = runtime.RunWorldProgram(*program, entryPoint, ticks, dt, optLevel);

    if (result.kind == ce::lang::jit::ResultKind::Error) {
        std::cerr << "celc: " << result.errorMessage << std::endl;
        return 1;
    }

    switch (result.kind) {
        case ce::lang::jit::ResultKind::Int: std::cout << result.intValue << std::endl; break;
        case ce::lang::jit::ResultKind::Float: std::cout << result.floatValue << std::endl; break;
        case ce::lang::jit::ResultKind::Bool: std::cout << (result.boolValue ? "true" : "false") << std::endl; break;
        case ce::lang::jit::ResultKind::Void: std::cout << "(void)" << std::endl; break;
        case ce::lang::jit::ResultKind::Error: break; // handled above.
    }

    if (result.faulted) {
        std::cerr << "celc: script faulted: " << result.faultMessage << std::endl;
        return 1;
    }
    return 0;
}

// GS6's headless verification path for the REAL production pipeline --
// ce::engine::Simulation::Step + ce::engine::ScriptComponent + a real
// ce::lang::jit::CelScriptRuntime, exactly what CreationEngineEditor and
// CreationEngineServer use, rather than celc's own --run/--run-world
// (which call Runtime::CompileAndRun/RunWorldProgram directly and don't
// exercise Simulation::Step or ScriptComponent at all). The script must
// define on_start(self: entity) and/or on_tick(self: entity, dt: float)
// (see docs/SCRIPTING_ABI.md) -- a single entity is spawned and given a
// ScriptComponent, then Simulation::Step is called `ticks` times.
// Prints the entity's final Transform.position (raw, full precision) on
// one line, then a rounded x^2+z^2 checksum on a second line -- the
// SAME two-line format CreationEngineServer's own --script/--ticks batch
// mode prints, so a CTest can diff the two tools' output for bit-for-bit
// parity (see run_simulation_parity_test.cmake) as well as check the
// checksum against a hand-computed .expected fixture.
//
// Shared by --run-simulation (source read from a .cel file) and
// --run-graph (source generated in-memory from a .celg graph, GS9) --
// both exercise the identical real production pipeline from this point
// on, which is exactly the property GS9's own verification relies on:
// a hand-written .cel and a graph-generated one that describe the same
// behavior must produce byte-identical output here.
int RunSimulationFromSource(const std::string& source, int ticks, float dt) {
    auto runtime = ce::lang::jit::CreateScriptRuntime();
    std::string error;
    auto compiled = runtime->Compile(source, error);
    if (compiled == nullptr) {
        std::cerr << "celc: " << error << std::endl;
        return 1;
    }

    ce::engine::World world;
    world.SetScriptRuntime(runtime);
    const entt::entity entity = world.CreateEntity();
    world.Registry().emplace<ce::engine::ScriptComponent>(entity, ce::engine::ScriptComponent{ compiled });

    for (int i = 0; i < ticks; ++i) {
        ce::engine::Simulation::Step(world, dt);
    }

    bool faulted = false;
    std::string faultMessage;
    {
        std::lock_guard<std::mutex> lock(world.RegistryMutex());
        if (world.Registry().valid(entity)) {
            const auto& script = world.Registry().get<ce::engine::ScriptComponent>(entity);
            faulted = script.faulted;
            faultMessage = script.faultMessage;

            const auto* transform = world.Registry().try_get<ce::engine::Transform>(entity);
            const ce::engine::Vec3 p = transform != nullptr ? transform->position : ce::engine::Vec3{};
            std::cout << std::setprecision(9) << p.x << " " << p.y << " " << p.z << "\n";
            std::cout << std::floor(p.x * p.x + p.z * p.z + 0.5f) << std::endl;
        }
    }

    if (faulted) {
        std::cerr << "celc: script faulted: " << faultMessage << std::endl;
        return 1;
    }
    return 0;
}

int RunSimulation(const std::string& path, int ticks, float dt) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }
    std::ostringstream sourceStream;
    sourceStream << file.rdbuf();
    return RunSimulationFromSource(sourceStream.str(), ticks, dt);
}

// Loads a .celg file and validates it against the real v1 node catalog
// (BuildCoreNodeCatalog), or nullptr with an error already printed to
// stderr on failure -- shared by --graph-to-source and --run-graph.
std::unique_ptr<ce::node_system::Graph> LoadGraph(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return nullptr;
    }
    std::ostringstream sourceStream;
    sourceStream << file.rdbuf();

    std::string error;
    auto graph = ce::node_system::DeserializeGraph(sourceStream.str(), error);
    if (!graph) {
        std::cerr << "celc: " << error << std::endl;
        return nullptr;
    }
    return graph;
}

// --graph-to-source <file.celg> [--trace]: GS9's own headless entry
// point into ce_lang_nodegen -- loads a graph, generates CEL source text
// from it against the real v1 node catalog, and prints the result to
// stdout (or every codegen error to stderr, one per line, on failure).
// This is what a future "View Generated Code" pane in GS10's node
// editor calls under the hood. --trace (GS11) inserts a real runtime
// trace call at the start of every reached node -- see
// lang/nodegen/graph_to_source.h's own comment.
int RunGraphToSource(const std::string& path, bool trace) {
    auto graph = LoadGraph(path);
    if (!graph) {
        return 1;
    }
    const auto registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    const auto result = ce::lang::nodegen::GenerateSource(*graph, registry, { trace });
    if (!result.ok) {
        for (const std::string& err : result.errors) {
            std::cerr << "celc: " << err << std::endl;
        }
        return 1;
    }
    std::cout << result.source;
    return 0;
}

// --run-graph <file.celg> [--ticks N] [--dt D] [--trace]: generates
// source from the graph exactly like --graph-to-source, then runs it
// through the SAME real production pipeline --run-simulation uses
// (RunSimulationFromSource) rather than a separate graph-walking
// interpreter -- see docs/CAPABILITIES.md section 4.1's "compile
// authored graphs down to fast native execution rather than
// interpreting them node-by-node." This is GS9's own flagship
// verification vehicle: run this on a graph and --run-simulation on a
// hand-written .cel describing the same behavior, and diff the two
// tools' identical two-line output (see
// Language/tests/nodegen/run_nodegen_parity_test.cmake).
int RunRunGraph(const std::string& path, int ticks, float dt, bool trace) {
    auto graph = LoadGraph(path);
    if (!graph) {
        return 1;
    }
    const auto registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    const auto result = ce::lang::nodegen::GenerateSource(*graph, registry, { trace });
    if (!result.ok) {
        for (const std::string& err : result.errors) {
            std::cerr << "celc: " << err << std::endl;
        }
        return 1;
    }
    return RunSimulationFromSource(result.source, ticks, dt);
}

// --check-graph-diagnostics <file.celg>: GS11's headless entry point
// into ce::lang::nodegen::CheckGeneratedSource -- generates source from
// the graph, then runs it through parse+sema (no JIT) and maps every
// resulting diagnostic back to the node that produced it via the
// generator's own source map, printing one `node <id>: CEL<code>:
// <message>` line per diagnostic (or `<no node>: ...` if a diagnostic's
// line didn't map to any node -- e.g. a file-scope `var` line). Exit 0
// with "OK" if generation AND checking both come back clean. This is
// the "a broken node input highlights that specific node, not an
// opaque error" requirement's own verification vehicle -- see
// Language/tests/run_graph_diagnostics_test.cmake.
int RunCheckGraphDiagnostics(const std::string& path) {
    auto graph = LoadGraph(path);
    if (!graph) {
        return 1;
    }
    const auto registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    const auto genResult = ce::lang::nodegen::GenerateSource(*graph, registry);
    if (!genResult.ok) {
        for (const std::string& err : genResult.errors) {
            std::cerr << "celc: " << err << std::endl;
        }
        return 1;
    }

    const auto checkResult = ce::lang::nodegen::CheckGeneratedSource(genResult);
    for (const auto& diag : checkResult.diagnostics) {
        std::cout << (diag.nodeId == 0 ? std::string("<no node>") : ("node " + std::to_string(diag.nodeId))) << ": CEL"
                   << static_cast<int>(diag.code) << ": " << diag.message << std::endl;
    }
    if (!checkResult.ok) {
        return 1;
    }
    std::cout << "OK" << std::endl;
    return 0;
}

// --measure-compile <file.cel>: times ce::engine::IScriptRuntime::Compile
// (parse+sema+IR+optimize+JIT, the exact same call ScriptPanel's
// background CompileJob makes -- Views/ScriptPanel.cpp) end to end on
// the real CelScriptRuntime, printing milliseconds. This is GS7's <1s
// compile-time requirement's own verification vehicle
// (run_compile_perf_test.cmake), measured for real rather than assumed
// from "it felt fast in the editor."
int RunMeasureCompile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }
    std::ostringstream sourceStream;
    sourceStream << file.rdbuf();

    auto runtime = ce::lang::jit::CreateScriptRuntime();
    std::string error;

    const auto start = std::chrono::steady_clock::now();
    auto compiled = runtime->Compile(sourceStream.str(), error);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();

    if (compiled == nullptr) {
        std::cerr << "celc: " << error << std::endl;
        return 1;
    }
    std::cout << elapsedMs << std::endl;
    return 0;
}

// --check-graph <file.celg>: loads a .celg file and re-serializes it to
// stdout -- GS8's format-stability regression vehicle. A committed
// fixture's own text, diffed against this command's output, must be
// byte-identical (see Language/tests/graph/run_celg_stability_test.cmake);
// any drift means DeserializeGraph/SerializeGraph stopped round-tripping
// exactly, which would silently corrupt every already-saved .celg file
// in the wild once GS10's node editor exists.
int RunCheckGraph(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "celc: cannot open " << path << std::endl;
        return 1;
    }
    std::ostringstream sourceStream;
    sourceStream << file.rdbuf();

    std::string error;
    auto graph = ce::node_system::DeserializeGraph(sourceStream.str(), error);
    if (!graph) {
        std::cerr << "celc: " << error << std::endl;
        return 1;
    }

    std::cout << ce::node_system::SerializeGraph(*graph);
    return 0;
}

// --dump-nodegen-fixture <name>: prints SerializeGraph(*Build...()) for
// one of nodegen_fixtures.h's programmatically-built graphs -- how the
// committed Language/tests/nodegen/*.celg fixtures were produced (and
// how to regenerate them if the node catalog's pin shapes ever change),
// not itself part of the verified GS9 pipeline (see nodegen_fixtures.h).
int RunDumpNodegenFixture(const std::string& name) {
    std::unique_ptr<ce::node_system::Graph> graph;
    if (name == "bounce") {
        graph = ce::lang::tools::BuildBounceGraph();
    } else if (name == "operator-coverage") {
        graph = ce::lang::tools::BuildOperatorCoverageGraph();
    } else if (name == "subgraph") {
        graph = ce::lang::tools::BuildSubgraphGraph();
    } else if (name == "diagnostic-mapping") {
        graph = ce::lang::tools::BuildDiagnosticMappingGraph();
    } else if (name == "trace-demo") {
        graph = ce::lang::tools::BuildTraceDemoGraph();
    } else {
        std::cerr << "celc: unknown nodegen fixture '" << name
                   << "' (expected 'bounce', 'operator-coverage', 'subgraph', 'diagnostic-mapping', or 'trace-demo')"
                   << std::endl;
        return 1;
    }
    std::cout << ce::node_system::SerializeGraph(*graph);
    return 0;
}

int RunEmitLLVM(const std::string& path) {
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ParseAndCheck(path, arena, diagnostics);
    if (program == nullptr) {
        return 1;
    }

    ce::lang::jit::Runtime runtime;
    std::cout << runtime.EmitLLVMIR(*program);
    return 0;
}

} // namespace

// celc -- the CEL compiler/test-driver binary. This is the project's
// single headless entry point into the Language module: every GS
// milestone from here on adds a subcommand (--dump-ast, --run,
// --run-world, --graph-to-source, ...) rather than spinning up a
// separate test target per milestone.
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--selftest-jit") {
        ce::lang::jit::Runtime runtime;
        const bool ok = runtime.RunSelfTest();
        std::cout << (ok ? "[celc] selftest-jit: PASS" : "[celc] selftest-jit: FAIL") << std::endl;
        return ok ? 0 : 1;
    }

    if (argc >= 2 && std::string(argv[1]) == "--selftest-graph") {
        return ce::lang::tools::RunSelfTestGraph();
    }

    if (argc >= 3 && std::string(argv[1]) == "--check-graph") {
        return RunCheckGraph(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--dump-ast") {
        return RunDumpAst(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--check") {
        std::string domains;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (std::string(argv[i]) == "--domains") {
                domains = argv[i + 1];
            }
        }
        return RunCheck(argv[2], domains);
    }

    if (argc >= 3 && std::string(argv[1]) == "--run") {
        std::string entryPoint = "main";
        int optLevel = 2;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--entry") {
                entryPoint = argv[i + 1];
            } else if (flag == "--opt") {
                optLevel = std::atoi(argv[i + 1]);
            }
        }
        return RunExec(argv[2], entryPoint, optLevel);
    }

    if (argc >= 3 && std::string(argv[1]) == "--run-world") {
        std::string entryPoint = "tick";
        int ticks = 1;
        float dt = 1.0f / 60.0f;
        int optLevel = 2;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--entry") {
                entryPoint = argv[i + 1];
            } else if (flag == "--ticks") {
                ticks = std::atoi(argv[i + 1]);
            } else if (flag == "--dt") {
                dt = static_cast<float>(std::atof(argv[i + 1]));
            } else if (flag == "--opt") {
                optLevel = std::atoi(argv[i + 1]);
            }
        }
        return RunWorld(argv[2], entryPoint, ticks, dt, optLevel);
    }

    if (argc >= 3 && std::string(argv[1]) == "--run-simulation") {
        int ticks = 1;
        float dt = 1.0f / 60.0f;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            if (flag == "--ticks") {
                ticks = std::atoi(argv[i + 1]);
            } else if (flag == "--dt") {
                dt = static_cast<float>(std::atof(argv[i + 1]));
            }
        }
        return RunSimulation(argv[2], ticks, dt);
    }

    if (argc >= 3 && std::string(argv[1]) == "--dump-nodegen-fixture") {
        return RunDumpNodegenFixture(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--graph-to-source") {
        bool trace = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--trace") {
                trace = true;
            }
        }
        return RunGraphToSource(argv[2], trace);
    }

    if (argc >= 3 && std::string(argv[1]) == "--run-graph") {
        int ticks = 1;
        float dt = 1.0f / 60.0f;
        bool trace = false;
        for (int i = 3; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--trace") {
                trace = true;
            } else if (flag == "--ticks" && i + 1 < argc) {
                ticks = std::atoi(argv[++i]);
            } else if (flag == "--dt" && i + 1 < argc) {
                dt = static_cast<float>(std::atof(argv[++i]));
            }
        }
        return RunRunGraph(argv[2], ticks, dt, trace);
    }

    if (argc >= 3 && std::string(argv[1]) == "--check-graph-diagnostics") {
        return RunCheckGraphDiagnostics(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--emit-llvm") {
        return RunEmitLLVM(argv[2]);
    }

    if (argc >= 3 && std::string(argv[1]) == "--measure-compile") {
        return RunMeasureCompile(argv[2]);
    }

    std::cout << "celc -- Creation Engine Language compiler/test-driver\n"
                 "usage:\n"
                 "  celc --selftest-jit\n"
                 "  celc --selftest-graph\n"
                 "  celc --dump-ast <file.cel>\n"
                 "  celc --check <file.cel> [--domains core,world]\n"
                 "  celc --check-graph <file.celg>\n"
                 "  celc --run <file.cel> [--entry NAME] [--opt 0-3]\n"
                 "  celc --run-world <file.cel> [--entry NAME] [--ticks N] [--dt D] [--opt 0-3]\n"
                 "  celc --run-simulation <file.cel> [--ticks N] [--dt D]\n"
                 "  celc --dump-nodegen-fixture <bounce|operator-coverage|subgraph|diagnostic-mapping|trace-demo>\n"
                 "  celc --graph-to-source <file.celg> [--trace]\n"
                 "  celc --run-graph <file.celg> [--ticks N] [--dt D] [--trace]\n"
                 "  celc --check-graph-diagnostics <file.celg>\n"
                 "  celc --measure-compile <file.cel>\n"
                 "  celc --emit-llvm <file.cel>\n";
    return 1;
}

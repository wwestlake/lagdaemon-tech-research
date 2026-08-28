#include "AudioGraphSelfTest.h"

#include <cmath>
#include <sstream>

#include "lang/compiler.h"
#include "lang/sema.h"
#include "lang/jit/runtime.h"

#include "AudioGraphCodegen.h"
#include "AudioNodeCatalog.h"

namespace cw::audionodes
{

SelfTestResult RunAudioGraphSelfTest()
{
    SelfTestResult result;

    constexpr float kLevel = 0.65f;
    constexpr float kTestPhase = 0.5f; // must match AudioGraphCodegen.cpp's kTestPhase

    auto registry = BuildAudioNodeCatalog();
    auto graph = BuildSineToOutputDemoGraph(registry, kLevel);

    const GraphToSourceResult generated = GenerateAudioSource(graph, registry);
    if (!generated.ok)
    {
        result.message = "codegen failed:";
        for (const auto& error : generated.errors)
            result.message += " " + error;
        return result;
    }

    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    std::istringstream sourceStream(generated.source);
    ce::lang::Program* program = ce::lang::ParseProgram(sourceStream, arena, diagnostics);
    if (program == nullptr || diagnostics.HasErrors())
    {
        result.message = "parse failed for generated source:\n" + generated.source;
        return result;
    }

    if (!ce::lang::AnalyzeProgram(*program, diagnostics))
    {
        result.message = "sema failed for generated source:\n" + generated.source;
        return result;
    }

    ce::lang::jit::Runtime runtime;
    const ce::lang::jit::ExecResult execResult = runtime.CompileAndRun(*program, "compute_sample", /*optLevel=*/0);
    if (execResult.kind != ce::lang::jit::ResultKind::Float)
    {
        result.message = "JIT execution failed: " + execResult.errorMessage;
        return result;
    }
    if (execResult.faulted)
    {
        result.message = "JIT execution faulted: " + execResult.faultMessage;
        return result;
    }

    result.computedSample = execResult.floatValue;
    result.expectedSample = std::sin(kTestPhase) * kLevel;

    const float difference = std::abs(result.computedSample - result.expectedSample);
    result.ok = difference < 0.0001f;
    result.message = result.ok
        ? "OK: graph -> CEL -> JIT produced " + std::to_string(result.computedSample)
              + ", matching native std::sin computation " + std::to_string(result.expectedSample)
        : "MISMATCH: JIT produced " + std::to_string(result.computedSample)
              + ", native expected " + std::to_string(result.expectedSample);
    return result;
}

} // namespace cw::audionodes

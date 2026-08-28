#pragma once

#include <memory>
#include <string>

#include <entt/entt.hpp>

namespace ce::engine {

class World;
struct ScriptContext;

// Opaque handle to one successfully compiled CEL program. EngineCore
// never looks inside it -- ce_lang_jit's implementation
// (Language/src/jit/script_runtime.cpp) is the only code that knows
// what's really in here (a dedicated LLJIT instance plus resolved
// on_start/on_tick function pointers). Exists so ScriptComponent can
// hold a compiled program without EngineCore depending on Language:
// Language already depends on EngineCore (it links engine_core to reach
// World/Transform/ScriptContext), so the reverse dependency would be
// circular.
class ICompiledScript {
public:
    virtual ~ICompiledScript() = default;
};

// Implemented once, by ce_lang_jit's CelScriptRuntime
// (Language/include/lang/jit/script_runtime.h's CreateScriptRuntime()).
// Whichever executable links both EngineCore and ce_lang_jit
// (CreationEngineEditor, CreationEngineServer) constructs one instance
// at startup and injects it via World::SetScriptRuntime -- that's the
// one place LLVM enters the picture; Simulation::Step (simulation.h)
// only ever talks to this abstract interface, never to LLVM directly.
class IScriptRuntime {
public:
    virtual ~IScriptRuntime() = default;

    // Parses + type-checks + JITs `source` (a whole .cel file's text).
    // Returns nullptr and fills `errorOut` with a human-readable message
    // on any failure (syntax, semantic, or codegen) -- the caller
    // decides what to do with a failed compile (surface it in a script
    // panel, refuse to attach a ScriptComponent, etc.); this interface
    // doesn't assume one.
    virtual std::shared_ptr<ICompiledScript> Compile(const std::string& source, std::string& errorOut) = 0;

    // Calls the compiled program's `on_start(self: entity)`, if it
    // defines one -- optional; a program without one is not an error,
    // CallStart is simply a no-op. `self` and `ctx` follow the same
    // host-ABI convention as every other CEL entry point (see
    // docs/SCRIPTING_ABI.md). Returns false (with ctx.faulted/
    // faultMessage set) on a runtime fault, e.g. the loop-budget
    // watchdog tripping.
    virtual bool CallStart(ICompiledScript& script, entt::entity self, ScriptContext& ctx) = 0;

    // Calls `on_tick(self: entity, dt: float)`, same optionality as
    // CallStart -- a program with no on_tick just never does anything
    // per-tick, which is a legitimate script (e.g. spawn-once-and-forget
    // logic entirely in on_start).
    virtual bool CallTick(ICompiledScript& script, entt::entity self, ScriptContext& ctx, float dt) = 0;
};

} // namespace ce::engine

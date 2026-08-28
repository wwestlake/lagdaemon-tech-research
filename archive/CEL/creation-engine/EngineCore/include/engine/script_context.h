#pragma once

#include "engine/world.h"
#include "lang/jit/script_context.h"

namespace ce::engine {

// The opaque object every compiled CEL function and intrinsic call
// implicitly receives as its first parameter (see
// docs/SCRIPTING_ABI.md) -- not a thread-local, so multiple Worlds (or
// multiple concurrent compilations) stay independent and reentrant.
// Callers are expected to hold `world->RegistryMutex()` for the whole
// duration any compiled CEL code runs against a given context --
// intrinsics (Language/src/jit/world_intrinsics.cpp) never lock it
// themselves, per the ABI's rule 6.
//
// Derives from the shared, host-agnostic ce::lang::jit::ScriptContext
// (loopBudget/faulted/faultMessage) rather than duplicating those
// fields -- shared/CEL's Core trampolines are compiled once against the
// base type and take `ce::lang::jit::ScriptContext*`, but every call
// site in Engine passes a `ce::engine::ScriptContext*`, which converts
// implicitly and safely since World/elapsedTime are appended after the
// inherited base, never inserted before it.
struct ScriptContext : ce::lang::jit::ScriptContext {
    World* world = nullptr;

    // Elapsed simulation time (seconds), advanced by the host driver
    // (celc's --run-world, later GS6's Simulation::Step) by a fixed `dt`
    // once per tick -- what the `world_time()` intrinsic reads. Lives
    // here rather than on World itself since World's own Tick counter is
    // an integer with no fixed real-world time unit attached to it.
    float elapsedTime = 0.0f;
};

} // namespace ce::engine

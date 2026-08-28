#pragma once

#include <string>

#include "lang/jit/runtime.h" // shared/CEL: ExecResult, Program

namespace ce::lang::jit {

// GS5: like shared/CEL's Runtime::CompileAndRun, but backed by a real,
// internally constructed ce::engine::World and ce::engine::ScriptContext
// that persist across every tick -- so spawned entities, set_position
// calls, and accumulated globals carry over from one call to the next
// exactly like a real running script would see. `entryPoint` is called
// once per tick, `ticks` times total; each tick, the World's tick
// counter is advanced and the context's elapsed-time accumulator
// increases by `dt` before the call. Stops early if the watchdog trips.
// Returns the LAST tick's result; ExecResult::faulted/faultMessage
// report whether the context ended up faulted.
//
// A free function (not a Runtime member) since it's Engine-specific --
// shared/CEL's Runtime class has no knowledge of World at all.
ExecResult RunWorldProgram(Program& program, const std::string& entryPoint, int ticks, float dt, int optLevel);

} // namespace ce::lang::jit

#pragma once

#include <cstdint>
#include <string>

namespace ce::lang::jit {

// Generic, host-agnostic script context every compiled CEL function
// receives as its implicit leading argument. This is the ONLY context
// type shared/CEL itself knows about -- it carries just enough state for
// the Core intrinsics (the watchdog) to work with no knowledge of any
// consuming app's world/entity model.
//
// A consuming app (e.g. Creation Engine) that needs richer per-tick state
// (a World pointer, elapsed time, ...) derives its own context type from
// this one -- see engine/script_context.h. That works because every Core
// trampoline shared/CEL compiles takes `ce::lang::jit::ScriptContext*`,
// and a derived-class pointer converts to a base-class pointer implicitly
// and safely, even though the trampoline was compiled without ever
// seeing the derived type.
struct ScriptContext {
    int64_t loopBudget = 10'000'000;
    bool faulted = false;
    std::string faultMessage;
};

} // namespace ce::lang::jit

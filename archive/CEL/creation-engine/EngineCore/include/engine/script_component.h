#pragma once

#include <memory>
#include <string>

#include "engine/script_runtime.h"

namespace ce::engine {

// Attached to any entity that should run a compiled CEL program every
// Simulation::Step tick. `compiled` is produced once (via
// IScriptRuntime::Compile) and can be shared across multiple entities
// running the same source -- the same "share the immutable asset data,
// not the per-instance state" pattern scene::MeshRenderer already uses
// for mesh/material.
struct ScriptComponent {
    std::shared_ptr<ICompiledScript> compiled;

    // Guards CallStart from firing more than once for this entity, even
    // across multiple Simulation::Step calls.
    bool started = false;

    // Sticky: once a script faults (a runtime fault, e.g. the
    // loop-budget watchdog), Simulation::Step stops calling into it
    // every subsequent tick rather than retrying something already
    // known to be broken.
    bool faulted = false;
    std::string faultMessage;
};

} // namespace ce::engine

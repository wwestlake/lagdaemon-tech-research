#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "lang/ast.h"

namespace ce::lang::jit {

// GS4: what a zero-argument CEL function's result looks like once
// JIT-executed. Deliberately not a llvm::GenericValue or similar --
// this header stays LLVM-free (see the class comment below).
enum class ResultKind { Int, Float, Bool, Void, Error };

struct ExecResult {
    ResultKind kind = ResultKind::Error;
    int64_t intValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;
    std::string errorMessage; // set iff kind == Error.

    // GS5: true if the runaway-script watchdog (or some other runtime
    // fault) tripped during execution -- `intValue`/`floatValue`/
    // `boolValue` still hold whatever the entry point returned before
    // the fault was noticed (the watchdog only stops the CURRENT
    // function early, see module_builder.cpp's EmitWatchdogCheck), so
    // callers that care about a fault should check this rather than
    // trust the returned value blindly.
    bool faulted = false;
    std::string faultMessage; // set iff faulted.
};

// Opaque JIT runtime -- owns an LLVM ORC LLJIT instance internally. No
// llvm/*.h type ever appears in this header, and none may be added to
// it: every LLVM include is confined to Language/src/jit/*.cpp, so
// nothing that includes this header (including JUCE-based code, once
// this links into the editor/server in GS6) can collide with LLVM's own
// headers/macros. This is a structural firewall, not a convention to
// remember.
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // GS1 self-test: hand-builds a tiny LLVM IR module (an `add`
    // function, and a `call_host` function that calls back into a host
    // trampoline registered the same way every real intrinsic will be
    // registered from GS5 onward), runs it through the O2 pass
    // pipeline, verifies it, JITs it, and executes both functions,
    // checking their results. This is the concrete, checkable proof
    // that vcpkg-installed LLVM actually links and JITs in this exact
    // toolchain -- the reason GS1 exists.
    bool RunSelfTest();

    // GS4: compiles an already-parsed-and-sema-checked `program` to LLVM
    // IR, runs it through the optimizer at `optLevel` (0-3), JITs it,
    // and calls the zero-argument function named `entryPoint`. Only
    // Int/Float/Bool/Void return types are supported (Vec3/Entity have
    // no meaningful "print the result" representation at this CLI-tool
    // level, and no GS4 test needs one -- they're exercised indirectly
    // via functions that return a float/int projection of a vec3
    // computation instead). Returns ResultKind::Error with a message on
    // any failure (unresolved entry point, unsupported construct,
    // verification failure, ...).
    // GS5: every compiled CEL function now takes an implicit leading
    // ScriptContext* (see module_builder.cpp's DeclareFunctions) -- this
    // calls `entryPoint` exactly once, through a real,
    // internally-constructed generic ce::lang::jit::ScriptContext. Safe
    // for any GS4-style pure-computation program (including ones with
    // loops, since GS5's watchdog check touches only
    // ctx->faulted/loopBudget), but a script that calls a World-domain
    // intrinsic here has nothing to dereference -- a host that needs a
    // real per-tick World (Creation Engine) builds its own equivalent of
    // this on top of a derived ScriptContext; see
    // apps/CreationEngine/Language/include/lang/jit/world_runtime.h's
    // free-function RunWorldProgram.
    ExecResult CompileAndRun(Program& program, const std::string& entryPoint, int optLevel);

    // GS4: compiles `program` to LLVM IR (no optimization, no JIT) and
    // returns its textual form, or an "error: ..." string on failure.
    // For human inspection only (`celc --emit-llvm`) -- deliberately
    // never snapshot-tested: IR text is too brittle across LLVM versions
    // to diff against a committed fixture.
    std::string EmitLLVMIR(Program& program);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ce::lang::jit

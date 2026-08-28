#include "intrinsic_trampolines.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "lang/jit/script_context.h"

// Every function here is the real implementation behind a Core-domain
// intrinsics.def cSymbol entry (Debug, plus atan2) and the watchdog
// tick function -- extern "C", noexcept, per the ABI's rules.
// `ce::lang::jit::ScriptContext*` is always the first parameter,
// matching every CEL function/intrinsic call's implicit leading
// argument (module_builder.cpp). This file has zero World/Entity/EnTT
// knowledge -- see apps/CreationEngine/Language/src/jit/world_intrinsics.cpp
// for the World-domain trampolines Engine registers on top of these.
//
// vec3 arguments/returns cross this boundary as `float*` (3 packed
// floats, x/y/z) rather than by value, per the ABI's rule 1. String
// arguments cross as an explicit (const char*, int64_t length) pair --
// CEL strings are literal-only (see type.h), so this never needs to
// represent an owned/heap string.

using ce::lang::jit::ScriptContext;

extern "C" {

// --- Math (the one Math intrinsic with no direct LLVM intrinsic) -----

float ce_atan2(ScriptContext*, float y, float x) noexcept {
    return std::atan2(y, x);
}

// --- Debug -----------------------------------------------------------

void ce_log(ScriptContext*, const char* msg, int64_t msgLen) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << std::endl;
}

void ce_log_int(ScriptContext*, const char* msg, int64_t msgLen, int64_t value) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << value << std::endl;
}

void ce_log_float(ScriptContext*, const char* msg, int64_t msgLen, float value) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << value << std::endl;
}

void ce_log_vec3(ScriptContext*, const char* msg, int64_t msgLen, const float* xyz) noexcept {
    std::cout << "[cel] " << std::string(msg, static_cast<size_t>(msgLen)) << "(" << xyz[0] << ", " << xyz[1] << ", "
               << xyz[2] << ")" << std::endl;
}

// --- Watchdog ----------------------------------------------------------

// Called once per loop iteration entered (module_builder.cpp inserts
// this at the top of every while/for body, i.e. every loop back-edge).
// Returns 0 ("abort") once the per-context budget is exhausted or the
// context is already faulted; the caller (generated IR) reacts by
// returning from the CURRENT function immediately -- a simple,
// non-exception-based unwind that's enough to stop a runaway script
// without needing SEH across JIT'd frames.
int32_t ce_watchdog_tick(ScriptContext* ctx) noexcept {
    if (ctx->faulted) {
        return 0;
    }
    if (ctx->loopBudget <= 0) {
        ctx->faulted = true;
        ctx->faultMessage = "CEL9001: script exceeded its loop-iteration budget (possible infinite loop)";
        return 0;
    }
    --ctx->loopBudget;
    return 1;
}

} // extern "C"

namespace ce::lang::jit {

std::vector<AbiSymbol> GetAbiTrampolines() {
    return {
        { "ce_atan2", reinterpret_cast<void*>(&ce_atan2) },
        { "ce_log", reinterpret_cast<void*>(&ce_log) },
        { "ce_log_int", reinterpret_cast<void*>(&ce_log_int) },
        { "ce_log_float", reinterpret_cast<void*>(&ce_log_float) },
        { "ce_log_vec3", reinterpret_cast<void*>(&ce_log_vec3) },
        { "ce_watchdog_tick", reinterpret_cast<void*>(&ce_watchdog_tick) },
    };
}

std::unordered_map<std::string, IntrinsicDomain> GetAbiSymbolDomains() {
    std::unordered_map<std::string, IntrinsicDomain> domains;
// Keyed by cSymbol (not `name`) to match GetAbiTrampolines()'s own key
// space -- RegisterAbiTrampolines looks up each AbiSymbol::name (which
// IS the cSymbol) against this map. When this translation unit is
// compiled as part of Creation Engine's build, the quoted include below
// resolves to Engine's OWN full (Core + World) intrinsics.def via
// include-path precedence, so this map naturally covers World-domain
// symbols too in that context -- no code here changes based on which
// intrinsics.def wins. The pure-IR math/vec3 intrinsics' cSymbol is an
// inert placeholder (their own name, per intrinsics.def's own comment)
// and never appears in GetAbiTrampolines()'s real list, so those
// entries here are simply never queried -- harmless, not a bug.
#define CEL_INTRINSIC0(name, cSymbol, purity, domain, ret) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC1(name, cSymbol, purity, domain, ret, p1) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC2(name, cSymbol, purity, domain, ret, p1, p2) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC3(name, cSymbol, purity, domain, ret, p1, p2, p3) domains[#cSymbol] = IntrinsicDomain::domain;
#define CEL_INTRINSIC4(name, cSymbol, purity, domain, ret, p1, p2, p3, p4) domains[#cSymbol] = IntrinsicDomain::domain;
#include "lang/intrinsics.def"
#undef CEL_INTRINSIC0
#undef CEL_INTRINSIC1
#undef CEL_INTRINSIC2
#undef CEL_INTRINSIC3
#undef CEL_INTRINSIC4
    return domains;
}

} // namespace ce::lang::jit

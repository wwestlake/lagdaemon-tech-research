#pragma once

#include <memory>

#include "engine/script_runtime.h"
#include "lang/type.h"

namespace ce::lang::jit {

// Constructs the real ce_lang_jit-backed ce::engine::IScriptRuntime --
// the one concrete implementation CreationEngineEditor/
// CreationEngineServer (and celc, for its own headless verification)
// each construct once at startup and inject via
// ce::engine::World::SetScriptRuntime, so EngineCore's Simulation::Step
// can compile and run CEL scripts without EngineCore itself ever
// depending on LLVM. This is a plain function returning the interface
// type (not a class) so this header -- like runtime.h -- stays
// LLVM-free; the concrete CelScriptRuntime class is private to
// script_runtime.cpp.
//
// `allowedDomains` (GS-Interop, default All()) is this host's
// capability profile -- see lang/type.h's IntrinsicDomain and
// docs/CROSS_APP_LANGUAGE_DOMAINS.md. Every script this runtime
// compiles is restricted to it, at both compile time (sema diagnostics)
// and JIT symbol resolution (defense in depth). The default preserves
// today's actual behavior for every existing caller (MainComponent.cpp,
// Server/Source/Main.cpp, celc's default subcommands) -- everything
// available, nothing gated, unless a host opts in.
std::shared_ptr<ce::engine::IScriptRuntime> CreateScriptRuntime(
    ce::lang::IntrinsicDomainSet allowedDomains = ce::lang::IntrinsicDomainSet::All());

} // namespace ce::lang::jit

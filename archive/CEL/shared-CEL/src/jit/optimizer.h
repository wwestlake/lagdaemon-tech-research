#pragma once

#include <llvm/IR/Module.h>

namespace ce::lang::jit {

// Runs LLVM's standard per-module pipeline over `module` at `optLevel`
// (0-3, clamped) in place. Shared by Runtime::RunSelfTest (GS1) and
// CompileAndRun (GS4) so there's one place that knows how to build the
// analysis managers -- private to ce_lang_jit's own .cpp files, never a
// public header, per the header-firewall rule (see
// include/lang/jit/runtime.h's comment).
void RunOptimizationPasses(llvm::Module& module, int optLevel);

} // namespace ce::lang::jit

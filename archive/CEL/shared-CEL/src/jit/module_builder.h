#pragma once

#include <memory>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "lang/ast.h"

namespace ce::lang::jit {

// Translates an already-parsed-and-sema-checked Program into an LLVM
// module (one llvm::Function per CEL FuncDecl, one llvm::GlobalVariable
// per CEL global var). `program` must have come from a successful
// AnalyzeProgram() call -- every Expr::type / Stmt::resolvedType /
// GlobalVarDecl::resolvedType is assumed already resolved; this does no
// type-checking of its own; a program AnalyzeProgram rejected produces
// undefined results here, not a graceful failure.
//
// Returns nullptr and fills `errorOut` if `program` uses something this
// pass doesn't (yet) support -- currently, any intrinsic outside the
// "pure computation" math/vec3 set (world/entity/transform/debug
// intrinsics, deferred to GS5's host-ABI work).
std::unique_ptr<llvm::Module> BuildModule(llvm::LLVMContext& context, const std::string& moduleName,
                                           Program& program, std::string& errorOut);

} // namespace ce::lang::jit

#include "optimizer.h"

#include <llvm/Passes/PassBuilder.h>

namespace ce::lang::jit {

void RunOptimizationPasses(llvm::Module& module, int optLevel) {
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAM;
    llvm::FunctionAnalysisManager functionAM;
    llvm::CGSCCAnalysisManager cgsccAM;
    llvm::ModuleAnalysisManager moduleAM;
    passBuilder.registerModuleAnalyses(moduleAM);
    passBuilder.registerCGSCCAnalyses(cgsccAM);
    passBuilder.registerFunctionAnalyses(functionAM);
    passBuilder.registerLoopAnalyses(loopAM);
    passBuilder.crossRegisterProxies(loopAM, functionAM, cgsccAM, moduleAM);

    llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;
    switch (optLevel) {
        case 0: level = llvm::OptimizationLevel::O0; break;
        case 1: level = llvm::OptimizationLevel::O1; break;
        case 3: level = llvm::OptimizationLevel::O3; break;
        default: level = llvm::OptimizationLevel::O2; break;
    }

    auto modulePassManager = passBuilder.buildPerModuleDefaultPipeline(level);
    modulePassManager.run(module, moduleAM);
}

} // namespace ce::lang::jit

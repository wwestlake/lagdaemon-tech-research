#include "lang/jit/script_runtime.h"

#include <sstream>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetSelect.h>

#include <entt/entt.hpp>

#include "abi_registration.h"
#include "engine/script_context.h"
#include "lang/ast.h"
#include "lang/compiler.h"
#include "lang/diagnostics.h"
#include "lang/jit/world_intrinsics.h"
#include "lang/sema.h"
#include "module_builder.h"
#include "optimizer.h"

// This translation unit (and runtime.cpp/module_builder.cpp/optimizer.cpp/
// abi_registration.cpp alongside it) are the only places in the whole
// project an llvm/*.h header is allowed to appear -- see runtime.h's own
// comment. engine/script_runtime.h (the public interface this
// implements) is deliberately LLVM-free for exactly that reason.

namespace ce::lang::jit {

namespace {

int64_t FromEntity(entt::entity e) {
    return static_cast<int64_t>(static_cast<entt::id_type>(e));
}

using OnStartFn = void (*)(ce::engine::ScriptContext*, int64_t);
using OnTickFn = void (*)(ce::engine::ScriptContext*, int64_t, float);

// Holds the one thing EngineCore's ICompiledScript interface promises
// nothing about: a dedicated LLJIT instance (each compiled script owns
// its own JIT session, so hot-swapping one script's compiled module --
// GS7 -- never disturbs another entity's) plus the resolved on_start/
// on_tick function pointers, nullptr if the program didn't define one
// (both are optional, see script_runtime.h's interface comment).
class CompiledCelScript : public ce::engine::ICompiledScript {
public:
    std::unique_ptr<llvm::orc::LLJIT> lljit;
    OnStartFn onStart = nullptr;
    OnTickFn onTick = nullptr;
};

// Requires `name`, if the program defines it at all, to have EXACTLY the
// fixed lifecycle shape (on_start: one `entity` param, void return;
// on_tick: `entity` then `float`, void return) -- CelScriptRuntime
// resolves and calls these through a fixed native function-pointer
// signature (OnStartFn/OnTickFn above), so a same-named function with a
// different shape would be a real, silent ABI mismatch (wrong argument
// count/types) if it were allowed through uncaught. Returns true if
// absent (both hooks are optional) or present with the exact expected
// shape; fills `errorOut` and returns false only for a present-but-wrong-shape
// definition.
bool ValidateLifecycleSignature(ce::lang::Program& program, const std::string& name,
                                 const std::vector<ce::lang::Type>& expectedParams, std::string& errorOut) {
    for (ce::lang::Decl* decl : program.decls) {
        if (decl->kind != ce::lang::DeclKind::Func || decl->funcDecl->name != name) {
            continue;
        }
        ce::lang::FuncDecl& f = *decl->funcDecl;
        const bool returnsVoid = f.returnType.empty() || ce::lang::ParseTypeName(f.returnType) == ce::lang::Type::Void;
        bool paramsMatch = f.params.size() == expectedParams.size();
        for (std::size_t i = 0; paramsMatch && i < expectedParams.size(); ++i) {
            paramsMatch = ce::lang::ParseTypeName(f.params[i].type) == expectedParams[i];
        }
        if (!returnsVoid || !paramsMatch) {
            errorOut = name + "() must be `func " + name + "(self: entity" +
                       (expectedParams.size() > 1 ? ", dt: float" : "") + ")` returning void";
            return false;
        }
        return true;
    }
    return true; // absent entirely -- both lifecycle hooks are optional.
}

class CelScriptRuntime : public ce::engine::IScriptRuntime {
public:
    explicit CelScriptRuntime(ce::lang::IntrinsicDomainSet allowedDomains) : allowedDomains_(allowedDomains) {
        // Idempotent process-wide LLVM native-target registration --
        // same one-time-guard pattern as Runtime::Runtime() (runtime.cpp),
        // needed here too since a CelScriptRuntime can be constructed
        // without ever touching a ce::lang::jit::Runtime instance.
        static const bool initialized = [] {
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();
            llvm::InitializeNativeTargetAsmParser();
            return true;
        }();
        (void)initialized;
    }

    std::shared_ptr<ce::engine::ICompiledScript> Compile(const std::string& source, std::string& errorOut) override {
        // Only needed for the duration of this function -- once
        // BuildModule has emitted LLVM IR below, the AST itself is no
        // longer referenced by anything the compiled script keeps
        // (matches celc's own ParseAndCheck/RunExec pattern, which never
        // keeps the arena beyond one function's scope either).
        ce::lang::AstArena arena;
        std::istringstream stream(source);
        ce::lang::DiagnosticEngine diagnostics;
        ce::lang::Program* program = ce::lang::ParseProgram(stream, arena, diagnostics);
        if (program == nullptr || diagnostics.HasErrors()) {
            errorOut = FormatDiagnostics(diagnostics);
            return nullptr;
        }
        if (!ce::lang::AnalyzeProgram(*program, diagnostics, allowedDomains_)) {
            errorOut = FormatDiagnostics(diagnostics);
            return nullptr;
        }

        if (!ValidateLifecycleSignature(*program, "on_start", { ce::lang::Type::Entity }, errorOut)) {
            return nullptr;
        }
        if (!ValidateLifecycleSignature(*program, "on_tick", { ce::lang::Type::Entity, ce::lang::Type::Float },
                                         errorOut)) {
            return nullptr;
        }

        auto context = std::make_unique<llvm::LLVMContext>();
        std::string buildError;
        auto module = BuildModule(*context, "cel_script", *program, buildError);
        if (module == nullptr) {
            errorOut = buildError;
            return nullptr;
        }
        RunOptimizationPasses(*module, /*optLevel=*/2);

        auto script = std::make_shared<CompiledCelScript>();
        auto jitOrErr = llvm::orc::LLJITBuilder().create();
        if (!jitOrErr) {
            errorOut = llvm::toString(jitOrErr.takeError());
            return nullptr;
        }
        script->lljit = std::move(*jitOrErr);

        if (auto err = RegisterAbiTrampolines(*script->lljit, allowedDomains_, GetWorldAbiTrampolines())) {
            errorOut = llvm::toString(std::move(err));
            return nullptr;
        }

        llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
        if (auto err = script->lljit->addIRModule(std::move(tsm))) {
            errorOut = llvm::toString(std::move(err));
            return nullptr;
        }

        if (auto sym = script->lljit->lookup("on_start")) {
            script->onStart = sym->toPtr<OnStartFn>();
        } else {
            llvm::consumeError(sym.takeError()); // absent -- optional, not an error.
        }
        if (auto sym = script->lljit->lookup("on_tick")) {
            script->onTick = sym->toPtr<OnTickFn>();
        } else {
            llvm::consumeError(sym.takeError());
        }

        return script;
    }

    bool CallStart(ce::engine::ICompiledScript& script, entt::entity self, ce::engine::ScriptContext& ctx) override {
        auto& impl = static_cast<CompiledCelScript&>(script);
        if (impl.onStart != nullptr) {
            impl.onStart(&ctx, FromEntity(self));
        }
        return !ctx.faulted;
    }

    bool CallTick(ce::engine::ICompiledScript& script, entt::entity self, ce::engine::ScriptContext& ctx,
                  float dt) override {
        auto& impl = static_cast<CompiledCelScript&>(script);
        if (impl.onTick != nullptr) {
            impl.onTick(&ctx, FromEntity(self), dt);
        }
        return !ctx.faulted;
    }

private:
    static std::string FormatDiagnostics(const ce::lang::DiagnosticEngine& diagnostics) {
        std::ostringstream out;
        diagnostics.PrintAll(out);
        return out.str();
    }

    ce::lang::IntrinsicDomainSet allowedDomains_;
};

} // namespace

std::shared_ptr<ce::engine::IScriptRuntime> CreateScriptRuntime(ce::lang::IntrinsicDomainSet allowedDomains) {
    return std::make_shared<CelScriptRuntime>(allowedDomains);
}

} // namespace ce::lang::jit

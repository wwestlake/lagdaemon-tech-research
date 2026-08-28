#include "lang/jit/world_runtime.h"

#include <entt/entt.hpp>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetSelect.h>

#include "abi_registration.h"
#include "engine/script_context.h"
#include "engine/world.h"
#include "lang/ast.h"
#include "lang/jit/world_intrinsics.h"
#include "module_builder.h"
#include "optimizer.h"

// This translation unit is one of the places an llvm/*.h header is
// allowed to appear on the Engine side of the language boundary -- see
// shared/CEL's runtime.h's own comment for the general rule.

namespace ce::lang::jit {

ExecResult RunWorldProgram(Program& program, const std::string& entryPoint, int ticks, float dt, int optLevel) {
    // `entryPoint` must be zero-argument OR take exactly one `self:
    // entity` parameter -- the self-threading pattern real CEL scripts
    // use for the on_start/on_tick lifecycle: a script can't construct
    // or store an entity handle any other way (no entity literal
    // syntax, no int->entity cast, and a module-global can't be
    // entity-typed since DeclareGlobals requires a literal initializer
    // -- see intrinsics.def's find_by_name comment for why find_by_name
    // can't fill that gap either). When self-threading is used, a
    // zero-argument `init() -> entity` function (if the program defines
    // one) is called exactly once before the tick loop, and its result
    // is threaded into every `entryPoint` call as `self`.
    FuncDecl* entryDecl = nullptr;
    for (Decl* decl : program.decls) {
        if (decl->kind == DeclKind::Func && decl->funcDecl->name == entryPoint) {
            entryDecl = decl->funcDecl;
            break;
        }
    }
    if (entryDecl == nullptr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, "no function named '" + entryPoint + "'" };
    }
    bool takesSelf = false;
    if (entryDecl->params.size() == 1 && ParseTypeName(entryDecl->params[0].type) == Type::Entity) {
        takesSelf = true;
    } else if (!entryDecl->params.empty()) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false,
                            "entry point '" + entryPoint +
                                "' must take zero arguments, or exactly one 'entity' parameter (self)" };
    }
    const Type returnType = entryDecl->returnType.empty() ? Type::Void : ParseTypeName(entryDecl->returnType);
    if (returnType != Type::Int && returnType != Type::Float && returnType != Type::Bool && returnType != Type::Void) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false,
                            "entry point '" + entryPoint + "' returns " + std::string(ToString(returnType)) +
                                ", which --run-world can't print (only int/float/bool/void are supported)" };
    }

    bool hasInit = false;
    for (Decl* decl : program.decls) {
        if (decl->kind == DeclKind::Func && decl->funcDecl->name == "init" && decl->funcDecl->params.empty() &&
            !decl->funcDecl->returnType.empty() && ParseTypeName(decl->funcDecl->returnType) == Type::Entity) {
            hasInit = true;
            break;
        }
    }

    static const bool initialized = [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        return true;
    }();
    (void)initialized;

    auto context = std::make_unique<llvm::LLVMContext>();
    std::string buildError;
    auto module = BuildModule(*context, "cel_module", program, buildError);
    if (module == nullptr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, buildError };
    }

    RunOptimizationPasses(*module, optLevel);

    auto jitOrErr = llvm::orc::LLJITBuilder().create();
    if (!jitOrErr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(jitOrErr.takeError()) };
    }
    std::unique_ptr<llvm::orc::LLJIT> lljit = std::move(*jitOrErr);

    if (auto err = RegisterAbiTrampolines(*lljit, IntrinsicDomainSet::All(), GetWorldAbiTrampolines())) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(std::move(err)) };
    }

    llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(context));
    if (auto err = lljit->addIRModule(std::move(tsm))) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(std::move(err)) };
    }

    auto symOrErr = lljit->lookup(entryPoint);
    if (!symOrErr) {
        return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(symOrErr.takeError()) };
    }

    // A single, real World/ScriptContext shared across every tick this
    // call makes -- entities spawned or positions set on tick 3 are
    // still there on tick 4, exactly like a real running script would
    // expect. No RegistryMutex lock here: celc is single-threaded, and
    // the real multi-threaded caller (GS6's Simulation::Step) is the one
    // actually required to hold it around calls like this.
    ce::engine::World world;
    ce::engine::ScriptContext ctx;
    ctx.world = &world;

    // Entity's LLVM representation is a plain i64 (MapType) -- entt::null
    // is the "no entity yet" sentinel `is_valid()` correctly rejects,
    // used only if `takesSelf` is set but the program defines no init().
    int64_t selfValue = static_cast<int64_t>(static_cast<entt::id_type>(entt::entity{ entt::null }));
    if (hasInit) {
        auto initSymOrErr = lljit->lookup("init");
        if (!initSymOrErr) {
            return ExecResult{ ResultKind::Error, 0, 0.0f, false, llvm::toString(initSymOrErr.takeError()) };
        }
        auto* initFn = initSymOrErr->toPtr<int64_t (*)(ce::engine::ScriptContext*)>();
        selfValue = initFn(&ctx);
    }

    ExecResult last{ ResultKind::Void, 0, 0.0f, false, {}, false, {} };
    for (int tick = 0; tick < ticks; ++tick) {
        world.AdvanceTick();
        ctx.elapsedTime += dt;

        switch (returnType) {
            case Type::Int: {
                if (takesSelf) {
                    auto* fn = symOrErr->toPtr<int64_t (*)(ce::engine::ScriptContext*, int64_t)>();
                    last = ExecResult{ ResultKind::Int, fn(&ctx, selfValue), 0.0f, false, {}, ctx.faulted, ctx.faultMessage };
                } else {
                    auto* fn = symOrErr->toPtr<int64_t (*)(ce::engine::ScriptContext*)>();
                    last = ExecResult{ ResultKind::Int, fn(&ctx), 0.0f, false, {}, ctx.faulted, ctx.faultMessage };
                }
                break;
            }
            case Type::Float: {
                if (takesSelf) {
                    auto* fn = symOrErr->toPtr<float (*)(ce::engine::ScriptContext*, int64_t)>();
                    last = ExecResult{ ResultKind::Float, 0, fn(&ctx, selfValue), false, {}, ctx.faulted, ctx.faultMessage };
                } else {
                    auto* fn = symOrErr->toPtr<float (*)(ce::engine::ScriptContext*)>();
                    last = ExecResult{ ResultKind::Float, 0, fn(&ctx), false, {}, ctx.faulted, ctx.faultMessage };
                }
                break;
            }
            case Type::Bool: {
                if (takesSelf) {
                    auto* fn = symOrErr->toPtr<bool (*)(ce::engine::ScriptContext*, int64_t)>();
                    last = ExecResult{ ResultKind::Bool, 0, 0.0f, fn(&ctx, selfValue), {}, ctx.faulted, ctx.faultMessage };
                } else {
                    auto* fn = symOrErr->toPtr<bool (*)(ce::engine::ScriptContext*)>();
                    last = ExecResult{ ResultKind::Bool, 0, 0.0f, fn(&ctx), {}, ctx.faulted, ctx.faultMessage };
                }
                break;
            }
            case Type::Void: {
                if (takesSelf) {
                    auto* fn = symOrErr->toPtr<void (*)(ce::engine::ScriptContext*, int64_t)>();
                    fn(&ctx, selfValue);
                } else {
                    auto* fn = symOrErr->toPtr<void (*)(ce::engine::ScriptContext*)>();
                    fn(&ctx);
                }
                last = ExecResult{ ResultKind::Void, 0, 0.0f, false, {}, ctx.faulted, ctx.faultMessage };
                break;
            }
            default:
                return ExecResult{ ResultKind::Error, 0, 0.0f, false, "internal error: unreachable return type" };
        }

        if (ctx.faulted) {
            break;
        }
    }
    return last;
}

} // namespace ce::lang::jit

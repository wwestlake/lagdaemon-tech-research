# CEL Host ABI (GS5)

This document describes the boundary between JIT-compiled CEL code and
the host engine (`EngineCore`) — the rules every intrinsic, and every
CEL function, must follow so that generated LLVM IR, extern "C" host
trampolines, and the ORC JIT's symbol resolution all agree with each
other. It exists because none of this is checked by the CEL compiler
itself the way types or control flow are — get it wrong here and the
failure mode is a crash or silently wrong values inside JIT'd code, not
a diagnostic.

## The implicit `ScriptContext*` parameter

Every compiled CEL function — user-defined or intrinsic — takes an
implicit leading `ce::engine::ScriptContext*` parameter that never
appears in CEL source and that sema never sees (`FunctionSignature`
knows nothing about it; it's purely a `module_builder.cpp` codegen
detail, added in `DeclareFunctions`/`CodegenFunctionBody`). A `func`
that reads `func Foo(x: int) -> int` in CEL compiles to the LLVM
signature `i64 @Foo(ptr %__ctx, i64 %x)`.

`ScriptContext` is treated as **fully opaque** by generated IR — no
LLVM struct type ever mirrors its C++ layout, and no generated
instruction ever loads/stores through it directly. It is only ever
passed *through* — from one CEL call to the next, and to real host
trampolines. This is deliberate: hand-encoding `ScriptContext`'s memory
layout in LLVM IR would silently break the moment the C++ struct's
layout changed (added fields, reordered members, different padding
under a different compiler) with no verifier error to catch it. Every
*real* field access happens inside a genuine C++ trampoline
(`Language/src/jit/intrinsic_trampolines.cpp`), where the compiler
checks the access against the real struct.

`ScriptContext` is not a thread-local. Multiple `World`s (and multiple
concurrent compilations) stay independent and reentrant because the
pointer is threaded explicitly, not looked up implicitly.

Callers are expected to hold `world->RegistryMutex()` for the entire
duration any compiled CEL code runs against a context pointing at that
`world` — every intrinsic trampoline is written on the assumption the
lock is already held, and **never locks it itself** (rule 6 below).

## The six ABI rules

1. **Aggregates never cross by value.** `vec3` (an LLVM struct of three
   floats) is never passed or returned by value through a real
   `extern "C"` call. Arguments marshal through a stack-allocated
   `float[3]` and cross as a `float*`; a `vec3`-returning intrinsic gets
   an extra trailing `float*` out-parameter instead of a return value.
   This sidesteps MSVC x64's hidden-sret-pointer struct-return
   convention entirely rather than trying to replicate it from
   hand-built IR. (This rule is about the *host ABI boundary*
   specifically — a CEL function calling another CEL function still
   passes `vec3` by value, since that's an ordinary intra-module LLVM
   call the JIT never needs a C-compatible calling convention for.)
2. **Only a fixed set of types cross the boundary:** `int64_t` (CEL
   `int`), `float` (CEL `float`), `int32_t` as a widened `bool` (CEL
   `bool` — LLVM `i1` at the IR level, widened to `i32` at the call
   site and truncated back on return), `uint64_t` (CEL `entity`,
   opaque, no arithmetic), `const char* + int64_t` length pair (CEL
   `string` — always a direct literal, see below), `float*` (a `vec3`
   argument or out-param), and `ScriptContext*`.
3. **Every trampoline is `extern "C"` and `noexcept`.** JIT'd Win64
   frames carry no unwind info for a C++ exception to cross safely — a
   trampoline that let one escape would corrupt the caller's stack.
   Failures are reported through `ScriptContext::faulted` /
   `faultMessage`, never thrown.
4. **Generated IR carries no `invoke`, and every declared function is
   `nounwind`.** There is nothing here for an exception to unwind
   *into* — this is the generation-side mirror of rule 3.
5. **`ScriptContext*` is always the implicit first parameter** of every
   CEL function and every intrinsic call (see above).
6. **Intrinsics never lock `RegistryMutex()` themselves.** The script
   phase caller (`celc --run-world`'s driver today;
   `Simulation::Step` from GS6 onward) holds it for the whole duration
   any compiled CEL code runs. An intrinsic that tried to lock it too
   would deadlock the very call that's holding it.

## Strings are literal-only

CEL has no heap-owned or variable `string` value (see `type.h`) — a
string can only ever appear as a direct literal argument to an
intrinsic call (sema enforces this via `NonLiteralStringArgument`).
Because of that, string arguments never go through `CodegenExpr` at
all: `CodegenAbiCall` reads the literal's text directly off the AST
node and materializes it as a global constant string plus its
compile-time-known length, passed as the `(const char*, int64_t)` pair
from rule 2. There is no "string value" a CEL variable could ever hold.

## Purity and LLVM attributes

Every intrinsic in `intrinsics.def` has a purity column that drives one
LLVM memory-access attribute on its *declaration* in the generated
module (the declaration only — this module never defines these
functions; the real implementation lives in
`intrinsic_trampolines.cpp` and is resolved at JIT time):

| Purity       | LLVM attribute | Meaning                                                             |
|--------------|-----------------|----------------------------------------------------------------------|
| `Pure`       | `readnone`      | Touches no memory at all, including through `ScriptContext*`.       |
| `ReadsWorld` | `readonly`      | May read anything (including through `ScriptContext*`/`World`), never writes. |
| `Mutates`    | *(none)*        | May freely read and write.                                          |

This column describes whether the intrinsic touches the **World**, not
whether it writes its own return slot: a `Vec3`-returning intrinsic
(`get_position`, `get_rotation`, `get_scale`) writes through its
trailing out-param pointer (rule 1) regardless of what `intrinsics.def`
says its purity is, so `module_builder.cpp`'s `CodegenAbiCall` always
declares those as `Mutates` (no attribute) rather than trusting the
table's `ReadsWorld` for the LLVM declaration specifically. Declaring
one `readonly` anyway is a lie the optimizer takes literally: at `-O2`
it will legally treat the out-param store as never having happened and
elide it, leaving the destination uninitialized — this is not a
hypothetical, it's exactly the bug that made
`gs5_world_spawn_grid`/`gs5_world_orbit_steps` print `nan` before this
was caught.

Getting this wrong in the `ReadsWorld`/`Mutates` direction (marking,
say, `get_position` as `Pure`) would let LLVM's optimizer legally
hoist or reorder that call above a `set_position` call and silently
change observable game behavior — a real correctness bug, not just a
missed optimization. The 14 "pure computation" math/vec3 intrinsics
(`sqrt`, `vec3`, `dot`, ...) never reach this table at all: GS4 already
lowers them directly to LLVM IR (`CodegenBuiltinIntrinsic`) with no
external call and no ABI involved.

## The runaway-script watchdog

`module_builder.cpp` inserts a call to the real
`ce_watchdog_tick(ScriptContext*)` trampoline at the top of every
`while`/`for` loop body — i.e. every loop back-edge. It decrements
`ScriptContext::loopBudget` and returns whether to keep going; when the
budget is exhausted (or the context is already faulted for any other
reason), the *current function* returns immediately with a
zero-initialized value (there is no meaningful value to fabricate for a
fault, so a null one satisfies the verifier; callers are expected to
check `ScriptContext::faulted`, not trust the returned value). This is
a simple, non-exception-based unwind — sufficient to stop one runaway
script without needing SEH across JIT'd frames (rule 3/4 above rule
those out anyway). `ScriptContext::faultMessage` is set to
`"CEL9001: script exceeded its loop-iteration budget (possible infinite
loop)"`.

This is a **robustness measure against an accidental infinite loop, not
a security sandbox.** A `.cel` file is trusted-author-only content in
v1, the same trust level this project already extends to arbitrary
imported glTF/texture/audio assets. Untrusted third-party `.cel`/`.celg`
content (modding) is an explicitly deferred, pre-modding decision — see
the GS scripting plan's risk list.

## `find_by_name` is a stub

There is no framework-agnostic `Name` component in `EngineCore` yet
(`ce::scene::Name` is JUCE-facing, `Source/Scene/Components.h`,
unreachable from `Language/`'s trampolines) — `find_by_name` always
returns an invalid entity for now, deliberately, rather than blocking
the rest of the ABI on a name-lookup system this milestone's
verification scripts don't need. A real implementation is a follow-up
once `EngineCore` grows a framework-agnostic `Name` component.

## Why an entity can't be a module-global

CEL has no entity literal syntax and no `int`→`entity` cast, and a
module-level `var` requires a compile-time-constant literal
initializer (`DeclareGlobals` folds only literal int/float/bool
expressions — a call like `spawn()` is not foldable). That means the
*only* way v1 CEL can carry an entity's identity across two separate
calls into JIT'd code is by returning it from one call and passing it
into the next as an argument — the `init() -> entity` /
`tick(self: entity)` pattern `Language/tests/world/orbit_steps.cel`
uses, and the shape GS6's real `on_start`/`on_tick(self, dt)` lifecycle
will use for the same reason.

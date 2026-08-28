# CEL v2 Language Extensions — Design Spec

This document specifies the next round of CEL language work, agreed with
the user on 2026-08-02, immediately after CEL became a genuinely
suite-owned library (see Suite issue #33). It exists so the design
decisions made in that conversation aren't re-derived or contradicted by
a future session — every section below states a decision, not an open
brainstorm, except where explicitly marked open.

Tracking: GitHub issues #49–#57 on `wwestlake/Creation-Suite` (see each
section for its issue number). This document is the spec; the issues are
the work breakdown. If they ever disagree, this document is wrong and
needs fixing, since it was written after the issues in some cases and
should be the more complete statement of intent.

## 0. Governing principle

CEL is a general-purpose language first. Every restriction below (the
real-time-safe profile, the `rec` opt-in) is an **opt-in compilation
mode**, not a retroactive restriction on the language as a whole.
Engine gameplay scripts, general automation, and anything else CEL is
used for keep full access to unbounded loops, recursion, and (once
built) closures and dynamic allocation. Only code explicitly compiled
for the real-time-safe profile is restricted, and it is restricted
specifically because it runs on a deadline where an overrun is not
recoverable (an audio glitch, a dropped video frame), not because the
restriction is generally good practice.

## 1. Math types (#49, milestones #50–#55)

Extends the existing `vec3` pattern (a fixed-size float buffer that
crosses the host ABI as a raw `float*`, never by value, per the existing
rule 1 in the ABI doc) to a full small linear-algebra kit:

- `vec2`, `vec4` (#50) — same treatment as `vec3`: pure-IR arithmetic,
  member access via the existing postfix grammar (`.x .y .z .w`).
- `mat2`, `mat3`, `mat4` (#51) — fixed-size only, row/column convention
  decided when picked up and written into `SCRIPTING_ABI.md`. Pure-IR:
  add, subtract, scalar multiply, matrix×matrix, matrix×vector,
  transpose, identity.
- Matrix determinant/inverse (#52) — real host-ABI trampolines, not
  inlined (4x4 inverse is too much generated code to inline and keep
  CEL's sub-second compile guarantee). Singular-matrix behavior must be
  decided and tested explicitly, not left as undefined/NaN-propagation
  by accident.
- `quat` (#53) — **its own distinct type, not a 4x4 matrix.** Confirmed
  directly: quaternions get their own special operators (Hamilton
  product, conjugate, normalize, `slerp`, rotate-vec3) because that
  product is a different operation from matrix multiply and quat is the
  numerically stable, gimbal-lock-free representation this is for.
  Known follow-on (Creation-Engine, separate repo, separate scope):
  `Transform::eulerRotationRadians` is currently Euler angles, not a
  quaternion — only relevant once `set_rotation` needs to accept `quat`
  directly.
- Conversions + outer product (#54) — quat↔matrix, quat↔axis-angle
  (both directions), `outer(vec3, vec3) -> mat3`. Confirm real need
  before building every vec2/vec4 outer-product combination too — this
  is a likely scope-creep point.

## 2. Fixed-size arrays only — no dynamic lists in the base language (#55)

Confirmed directly: fixed-size arrays only. A dynamic-length list needs
an allocator, bounds-checked safety, and a runtime type story — a
distinct, larger feature, not a small addition. (Superseded in part by
§4 below: a genuine dynamic list becomes possible under the memory
model's "purchase" tier — see §4.3 — but the *base* language and the
real-time-safe profile only ever get fixed-size arrays.)

- Syntax/type-system design open at pickup time (e.g. `array<float, 8>`
  — exact spelling not pre-decided).
- Indexing is bounds-checked at runtime, faulting cleanly (same
  CEL9001-style fault mechanism as the loop watchdog) rather than
  reading/writing out of bounds — this is the one runtime check this
  feature needs, in exchange for needing no allocator at all.
- Element type restricted to scalars/vectors for the first version.

## 3. Functional-paradigm additions (#57)

### 3.1 Opt-in recursion via `rec`

By default, a function's call graph must be acyclic — checked
statically by sema. A function must be marked `rec` to participate in
any cycle:

```
func_decl:
    "func" "rec" IDENT "(" param_list_opt ")" "->" IDENT block
  | "func" "rec" IDENT "(" param_list_opt ")" block
  | "func" IDENT "(" param_list_opt ")" "->" IDENT block
  | "func" IDENT "(" param_list_opt ")" block
```

**Mutual recursion rule, confirmed directly: every function in a
call-graph cycle must be marked `rec`, not just one of them.** A reader
looking at a plain `func b()` must never have to trace into another
function to discover it's secretly recursive via `a`. Sema builds the
call graph and rejects any cycle containing an unmarked function.

### 3.2 Lambdas (no capture) — free

A lambda with no captured variables is just a function pointer with
anonymous syntax. Zero cost, no allocation, no new execution-model
concerns. Available everywhere, including the real-time-safe profile
(§5) in principle — but see §5's checker: rejected there anyway by
default for simplicity, pending real need to relax it.

### 3.3 Closures — non-escaping only, i.e. "lease"-tier (see §4.1)

A closure that captures enclosing variables must not outlive the stack
frame it was created in: no returning a closure from a function, no
storing one in a global or a field. Implemented as a (function pointer,
stack pointer) pair — the captured environment lives on the *caller's*
stack. This is the closure-specific instance of the general "lease"
memory tier in §4.

### 3.4 Guaranteed tail-call optimization

Implemented as an explicit AST-level rewrite (recognize a `rec`
function's self-call in tail position, transform it into a loop at
codegen time) rather than relying on LLVM's backend tail-call handling
— guaranteed behavior regardless of target calling-convention quirks
(Windows x64 tail calls are notably unreliable to depend on implicitly).
Same category of hand-verified compiler transformation as the existing
watchdog-check insertion at loop back-edges (`module_builder.cpp`'s
`EmitWatchdogCheck`).

## 4. Memory model: lease / rent / purchase

A three-tier ownership model, replacing what would otherwise be a
binary "stack only, ever" vs. "full heap, always" choice. Each tier has
a different cost and a different set of profiles it's legal in.

### 4.1 Lease — zero-cost, compile-time-scoped borrow

A borrowed pointer, statically checked to never outlive the lexical
scope that created it (a lightweight borrow-checker in sema, not a
runtime mechanism). Just a raw pointer at codegen time — zero runtime
representation beyond the pointer itself. Non-escaping closures (§3.3)
are the closure-specific instance of this tier. **Legal in every
profile, including real-time-safe (§5)** — it costs nothing, so there's
no reason to restrict it there.

### 4.2 Rent — pool-scoped, deterministic, bounded

Checked out from a pool the host allocates once up front (no `malloc`
during script execution, ever). Can outlive a single stack frame/call
(e.g. persist across multiple `tick()` calls within one script
instance's lifetime) but must eventually be returned — either via an
explicit intrinsic or automatically when the owning scope (the script
instance) is torn down. Bounded and deterministic, no GC, but real pool
bookkeeping cost. **Legal in the general-purpose profile.**

**Open question, not decided:** if the pool is fully reserved before a
real-time callback ever runs and "renting" is just handing out
already-reserved slots with zero allocation-time cost in the hot path,
this might be safe for the real-time-safe profile too. Left open —
resolve when this tier is actually implemented, don't assume either
answer.

### 4.3 Purchase — true ownership, real allocation

Full heap allocation, full lifetime transfer to the caller. This is
where a genuine **dynamic-length list** (explicitly excluded from the
base language in §2) gets a real home — gated to this tier, not banned
outright. Recommended default: no raw `free()`-style manual release;
tie a purchase's lifetime to its owning script instance and reclaim
everything when that instance is destroyed, avoiding the need for an
actual garbage collector while still supporting real dynamic
allocation. **Not legal in the real-time-safe profile at all** — same
bucket as unbounded loops/recursion/closures.

## 5. Real-time-safe execution profile — audio *and* video (#56)

Restated cleanly using §4's tiers: **the real-time-safe profile is
lease-tier only.** No rent, no purchase, no `rec`, no closures (even
though non-escaping closures are lease-tier and technically free —
excluded anyway by default for simplicity; revisit only on real need),
and every loop's trip count must be a compile-time constant (fully
unrollable), not merely watchdog-bounded. The scope is **not
audio-only** — it covers any hard-real-time render/processing path,
video included (Creation Movie, and Engine's per-frame path where
relevant).

Why this is stricter than the existing CEL9001 loop-budget watchdog: the
watchdog only catches genuinely infinite loops. A loop or recursive call
that terminates but does more work than fits in one callback's time
budget sails right through the watchdog and still glitches the output.
Real-time media needs the *worst case* cost knowable at compile time,
not just "eventually finite."

The sema check for this profile must produce diagnostics that name the
specific disallowed construct and why — this is meant to be a legible,
fast guardrail for AI-generated code as much as a human-facing compiler
error, since preventing an LLM from accidentally generating
real-time-unsafe code for an audio/video callback is a first-order goal
here, not an afterthought.

### 5.1 Per-module domain declaration, not a single global flag

Confirmed directly: which checks apply is decided **per module, via a
domain tag declared in source**, not by a single project-wide "compile
in real-time mode" switch, and not purely as external asset metadata —
revised after the user proposed concrete syntax:

```
module_decl:
    "module" IDENT IDENT "{" decl_list "}"
```

A module declaration names the module and tags it with a domain
identifier (`general`, `audio-dsp`, `sequencer`, `video-dsp`, ...),
wrapping the same `decl_list` the grammar already has. One file is one
`module` block. This is what lets one project mix a hard-real-time DSP
module with a non-real-time sequencer/orchestration module, each getting
exactly the checks appropriate to its declared domain, instead of an
all-or-nothing per-project choice — and because the tag lives in source,
it's visible exactly where the code lives and reviewed/versioned with
it, rather than living in a separate metadata record that can drift from
what the code actually does. When a module is saved as a `celModule`
asset (§6), its asset-catalog metadata is *derived* from this
in-source declaration at save time (for fast discovery/filtering without
re-parsing the module), not the other way around — source is the
primary declaration, the asset record mirrors it.

This must be **declared, verified, and enforced — not a self-reported
hint**:
- **Declared**: the module's metadata states its domain.
- **Verified**: the build/inspection process actually runs the §5
  checker against any module declaring a real-time domain and rejects
  the build if the declaration is false (code that doesn't actually
  satisfy the real-time-safe subset). A module can't claim "audio-dsp"
  and skip the checks that name implies.
- **Enforced at the host wiring point**: whatever host code plugs a
  compiled module into an actual real-time callback slot (an audio
  render callback, a video frame callback) must itself refuse to wire
  in a module that isn't verified real-time-safe for that slot — so a
  module mislabeled as `sequencer` can't be smuggled into the audio
  callback by the host simply trusting the label. The label is metadata
  the host checks, not documentation the host hopes is true.

Further scope, decided at pickup time: an audio/video capability domain
(mirroring Engine's existing `World` domain) excluding anything that can
block or do I/O — today's `log`/`log_int`/`log_float`/`log_vec3` call
`std::cout` and are not safe to call from a real-time callback as
currently implemented; sample/frame-buffer intrinsics, likely
block-based (N samples/pixels per call). `vec4` (§1) is a natural fit
for SIMD-lane-width DSP — worth designing the buffer intrinsics with
that in mind.

## 6. Generic app state: `set_state` / `get_state` (not yet a tracked issue)

A single Core-domain pair of intrinsics for occasional, configuration-
style app control — the default, low-ceremony way an app exposes
controllable state to CEL without needing a new bespoke intrinsic (and
a compiler change) every time it grows one more setting:

```
set_state(name: string, value: T)                  // global/app-level property
get_state(name: string) -> T

set_state(target: entity, name: string, value: T)   // per-object property
get_state(target: entity, name: string) -> T
```

**Two forms, not one — discovered while seeding Station's real registry
entries (see `apps/CreationStation/docs/STATION_CONTROL_REGISTRY_SEED.md`).**
A global property (`"tempo"`, `"master_gain"`) has a name that's the
whole story — the two-argument form applies. A per-object property
(a specific track's gain, mute, pan) is addressed by a *runtime* handle
(the `entity`/track handle returned by `create_track` or resolved via
LiteSemRAG grounding) plus a property name — the name (`"gain"`) is
still a compile-time literal resolved against the registry exactly as
before, but the target is an ordinary runtime argument, the same way
Engine's existing `set_position(entity, vec3)` already takes a runtime
entity handle for a compile-time-fixed operation. This doesn't reopen
the "not actually dynamically typed" property below — the property
*name* is still always known at compile time either way; only the
*target* varies by form.

**Not actually dynamically typed, despite the signature above.** CEL
strings are already literal-only (existing rule, `type.h`), so the
`name` argument at any call site is always a compile-time-known
constant. Sema resolves it against a suite-level registry —
`(appId, varName) -> { requiredType, onSet: handler }`, living alongside
`ProjectRegistry` (`shared/Interop`/`shared/Services`) — at *compile*
time, and generates an ordinary, fully statically-typed host-ABI call
for that exact concrete type. `set_state("tempo", 110.0)` and
`set_state("armed", true)` are each fully typed, unambiguous calls at
codegen time; there is no runtime type tag, no boxing, no dynamic
dispatch anywhere in the generated code. An app registers its own vars
(name, required type, and a handler invoked on set) at startup or
dynamically; whatever process compiles the script (a live app instance,
or an editor panel talking to one) already has that registry in memory
to query.

Naming: app-implicit by default (a script running inside Station targets
Station's own registry), with an explicit cross-app form only when a
script needs to reach into another app's registered state — matching
the "local unless you say otherwise" default used everywhere else in
this spec.

### 6.1 Scope boundary, confirmed directly: state vs. action, general vs. real-time

Two splits, not one:

- **State vs. action.** `set_state`/`get_state` is for things that are
  genuinely "a named variable with a value" (tempo, time signature, an
  armed flag, a monitor flag). It is NOT for actions that create or
  destroy something or need a richer multi-argument signature —
  `create_track(...)`, `add_plugin(...)` stay real, purpose-built
  intrinsics (or a future `invoke_command`-style dispatch, not decided
  here) because "create a track" isn't setting a value, it's producing
  a new handle with side effects.
- **General vs. real-time/performance-critical.** `set_state`/
  `get_state` is exclusively for the general-purpose profile. **Real-
  time-safe code (§5) never uses it, full stop** — even though the
  registry lookup itself is compile-time-resolved and technically
  zero-cost, the registry's `onSet` handler is still a layer of
  indirection whose cost isn't individually audited the way a
  hand-written trampoline is, which is exactly what the real-time-safe
  profile exists to rule out. Anything performance-sensitive or running
  in a real-time callback gets a purpose-built intrinsic that calls
  directly into the app's real API — the same "no unnecessary
  indirection" discipline the existing host-ABI rules already require
  of every trampoline.

## 7. Modules — saved, reused, referenced (not yet a tracked issue)

CEL stays single-compilation-unit (§0's "why not separate compilation"
reasoning: the whole point of the language is a sub-second full rebuild,
so separate/incremental compilation solves a problem CEL doesn't have).
"Modularity" here means source-level organization and **reuse across
projects and apps**, not a build/link system.

- Local import (same project): `import "vec_utils.cel" as vecutils;`
- **Asset-referenced import** (cross-project, cross-app):
  `import module("<asset-id>") as vecutils;` — resolved through the
  Suite's existing `shared/AssetSystem` catalog/resolver and, for
  cross-app cases, `shared/Interop`'s `ProjectRegistry` (the same
  discovery mechanism planned for e.g. Movie importing a Station audio
  stem — see Suite #13, currently unconsumed by any app). A CEL module
  becomes a new `AssetKind` (alongside `foleyPatch` etc.) rather than a
  bespoke module-resolution system — reusing infrastructure that
  already exists rather than inventing a parallel one.
- A "module asset" is reusable, addressable *source* (or a cached AST),
  not a separately linked binary — pulled in and compiled together with
  whatever imports it, every time, preserving the single-compilation-
  unit model.
- Qualified namespacing (`vecutils::foo`), not flat imports — avoids
  silent name collisions once independently-authored shared modules
  accumulate, and matters more as projects and shared libraries grow
  large (confirmed directly as the motivating case). A module's
  declared `name` (from `module_decl`, §5.1) **is** its namespace — no
  separate namespace keyword; one construct gives identity,
  domain-tagging, and namespacing together. This is purely a
  compile-time symbol-resolution concept: qualified names are resolved
  during the same single compile pass as everything else (§0's
  single-compilation-unit model), with zero runtime representation and
  zero performance cost — a namespace is a name-mangling scheme, not a
  runtime object.
- Visibility: leaning toward explicit-export-only (nothing visible
  across a module boundary unless marked exported) over C-style
  visible-by-default, for the same collision-avoidance reason — not yet
  finalized.
- Cross-file `rec` mutual-recursion cycle detection (§3.1) needs no
  special handling: since the whole program is still one compiled unit,
  the call graph doesn't care which file a function's text lives in.
- **Open, deliberately deferred:** versioning. What happens when a
  shared module a project depends on changes underneath it? Simplest
  starting answer is "always resolves to the asset's current state, no
  versioning" — acceptable as a v1 answer, not a final one.
- Not yet filed as a GitHub issue — do so before starting implementation
  (see the Documentation Index entry for this file in `AGENTS.md`, which
  is the pointer future sessions should follow back here).

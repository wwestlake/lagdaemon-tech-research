# Cross-App Language Domain Registry (GS-Interop)

This document is the cross-app "domain registry and intrinsic ownership
map" called for by step 4 of Creation Station's coordination doc
(`docs/Creation-Shared-Language-Rollout.md` in that repo). It exists
because CEL is meant to become the shared scripting language across all
four apps in the Creation suite — Creation Station, Creation Engine,
Creation Movie, Creation Live — but today only Creation Engine has a
real CEL compiler and JIT. This doc writes down, from what is actually
implemented here, the vocabulary the other three apps' `AppLanguagePolicy`
layers can eventually target instead of the hardcoded string allow-lists
they carry today.

For the asset/version/project-VFS side of that same cross-app contract,
see:

- [Cross-App Asset Interop Contract](./CROSS_APP_ASSET_INTEROP.md)

## What actually exists today

CEL's intrinsics (`Language/include/lang/intrinsics.def`) are tagged
with an `IntrinsicDomain` (`Language/include/lang/type.h`):

| `IntrinsicDomain` | Intrinsics |
|---|---|
| `Core` | `sqrt abs min max floor sin cos atan2 clamp lerp vec3 dot cross length normalize log log_int log_float log_vec3` — pure math/vec3 and debug logging; touches no `World` state. |
| `World` | `world_tick world_time spawn spawn_at destroy is_valid entity_count find_by_name get_position set_position get_rotation set_rotation get_scale set_scale` — everything that reads or writes `ScriptContext::world`. |

A host selects an `IntrinsicDomainSet` (a small bitset — `All()`,
`Only({...})`, `Contains()`) and passes it to `AnalyzeProgram` (sema)
and `RegisterAbiTrampolines` (JIT symbol resolution). A call to an
intrinsic outside the host's set is rejected at compile time
(`CEL2023`, `Language/include/lang/diagnostics.h`) and, as defense in
depth, its trampoline is never registered with the JIT even if
something slipped past sema. Both layers default to `All()` — every
existing caller (`celc`, the editor, the server) is unaffected unless it
opts into a narrower set. See `Language/tools/celc/main.cpp`'s
`--check <file> --domains core,world` flag for the exercisable CLI
surface, and `Language/tests/domain/` for the regression pair that
proves the default is truly unchanged behavior.

This is a real, enforced two-value taxonomy of what CEL can do *today*
— not a general capability system yet. The rest of this document is
the planned string vocabulary each app's rollout doc already
committed to, and how it maps onto `IntrinsicDomain` now versus later.

## Domain string vocabulary

Collected from each app's own `docs/LANGUAGE_ROLLOUT.md` (Movie, Live)
and `docs/Creation-Shared-Language-Rollout.md` (Station). Two are
implemented and enforced inside CEL right now; the rest are names other
apps have already picked for capabilities that don't exist inside CEL
yet — reserved so nobody collides on a name later, not a promise of
imminent implementation.

| Domain string | Status | `IntrinsicDomain` | Owning app(s) |
|---|---|---|---|
| `core` | **Implemented** | `Core` | All — pure math/vec3/logging, no per-app state. |
| `world` | **Implemented** | `World` | Creation Engine — entity/transform/world-tick access. |
| `gameplay` | Reserved | *(maps to `World` once named)* | Creation Engine |
| `physics` | Reserved | — | Creation Engine |
| `shared` | Reserved (alias) | *(maps to `Core`)* | All |
| `audio` | Reserved | — | Creation Station |
| `patch` | Reserved | — | Creation Station |
| `tracker` | Reserved | — | Creation Station |
| `mixer` | Reserved | — | Creation Station |
| `performance` | Reserved | — | Creation Station |
| `instrument` | Reserved | — | Creation Station |
| `movie` | Reserved | — | Creation Movie |
| `timeline` | Reserved | — | Creation Movie |
| `render` | Reserved | — | Creation Movie |
| `live` | Reserved | — | Creation Live |
| `scene` | Reserved | — | Creation Live |
| `broadcast` | Reserved | — | Creation Live |

`core`/`shared` are effectively the same concept under two names
picked independently by different rollout docs before this registry
existed; they're recorded as-is rather than silently merged, since
reconciling that naming is a follow-up for whoever writes the
string↔enum conversion function (see Non-goals).

Each app's own rollout doc already states which domains it needs and
which it must reject — reproduced here only as the union, not
re-derived; each app's doc remains the source of truth for its own
row:

- **Creation Station** (DAW): wants `shared, audio, patch, tracker,
  mixer, performance`; rejects `world, physics, broadcast, scene,
  render`.
- **Creation Engine** (this repo): implements `core, world` today,
  unconditionally (no gating existed before GS-Interop).
- **Creation Movie**: wants `shared, movie, timeline, render`; rejects
  `gameplay, world, physics, instrument, mixer, broadcast`.
- **Creation Live**: wants `shared, live, scene, broadcast`; rejects
  `gameplay, world, physics, instrument, mixer, timeline, render`.

## Capability manifests

The user's cross-app architecture note frames the goal as strict
app-specific execution boundaries via "explicit symbol resolution and
capability profiles," with an asset's embedded logic checked against
"metadata headers or host-enforced environment profiles" to decide
whether it may run in a given target app.

Inside CEL specifically, an app's **capability profile** *is* its
`IntrinsicDomainSet` — the set of domains it passes to `AnalyzeProgram`
and `RegisterAbiTrampolines` when compiling a script. A **capability
manifest** on an asset (e.g. metadata declaring "this script needs
`world`") is not built yet; nothing calls `IntrinsicDomainSet::Only`
with anything other than `All()` today outside the new domain tests.
When that lands, the natural shape is: read the manifest, build an
`IntrinsicDomainSet` from the domains it declares, intersect with the
host app's own allowed set, and compile with the result — the
enforcement mechanism (sema + JIT filtering) already exists and needs
no further change to support it.

## Non-goals (restated)

This registry does not, by itself:

- Extract `Language/` into its own repo or shared library. Per the
  rollout doc: "Do not try to force all four apps onto one giant repo
  or one giant runtime immediately."
- Touch Creation Station's language integration. Station's own bespoke
  Patina compiler stack has already been removed (Station now consumes
  the shared CEL frontend directly) — no host-dialect decision remains.
- Build a "CoreLib" (shared math/allocators/threading/serialization/
  logging) — a distinct, later concern.
- Add a string↔`IntrinsicDomain` conversion function inside CEL. No
  app has `ce_lang_jit` wired in yet to call one; adding it now would
  be speculative against callers that don't exist.
- Change build/toolchain/CMake discovery in this repo — that is
  Codex's assigned lane (shared VS2022/LLVM alignment across the
  suite) and is untouched by this work.

## Where this fits in the agreed sequencing

Per Creation Station's rollout doc:

1. Shared build environment — done, assigned to a different agent.
2. Scaffold Movie/Live with shared LLVM discovery + `AppLanguagePolicy`
   stub — done (both apps have `Language/AppLanguagePolicy.h/.cpp`
   today, a hardcoded allow-list with nothing backing it yet).
3. Add the shared LLVM helper to Creation Station — not this repo's
   job.
4. **Define the cross-app domain registry and intrinsic ownership
   map — this document, plus the `IntrinsicDomain`/`IntrinsicDomainSet`
   enforcement mechanism it describes.**
5. Consolidate compiler/runtime code across apps — explicitly not yet;
   each app keeps its own `AppLanguagePolicy` layer for now, with this
   doc as the shared vocabulary those layers can grow to target.

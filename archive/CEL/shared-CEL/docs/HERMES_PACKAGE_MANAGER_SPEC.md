# Hermes — CEL Package Manager & Distribution System

Quick-capture spec so this doesn't get lost — written immediately after
the CEL v2 language spec (`CEL_V2_LANGUAGE_SPEC.md`), which this
document depends on for the module/`module_decl`/asset-reference
concepts it builds on. Named directly by the user: Hermes, after the
Greek god of trade, travelers, and roads — the "get things reliably from
one place to another" god.

Tracking: not yet filed as GitHub issues — see `CEL_V2_LANGUAGE_SPEC.md`
section 7's side note on Suite #59 for the earlier, lighter mention of
this. File real issues once this spec is reviewed, rather than before.

## 1. Shape: Cargo's feature set, a GUI instead of a CLI

Confirmed directly: Hermes should work like Rust's Cargo (dependency
declaration, version resolution, a lockfile, a central registry,
publish/search/browse) but with **no command-line interface at all** —
every one of those operations happens through a GUI panel, presumably a
`shared/UI` component reusable across Engine/Movie/Station/Live the same
way `SuiteAiChatPanel` already is, so "add a dependency" is a search-and-
click action inside whichever Suite app the user is in, not a terminal
command.

## 2. The store: LagDaemon.com

A hosted registry at LagDaemon.com (confirmed available — "we can
arrange that"). Needs, at minimum:
- A backend service: search/list/download API, an authenticated publish
  endpoint, a database of module name → versions → metadata →
  dependency graph, and blob storage for the actual distributed
  artifacts (see §4).
- This is real, separate infrastructure work — a web service plus a
  storage backend — distinct from the CEL compiler/language work in
  `CEL_V2_LANGUAGE_SPEC.md`. Do not fold Hermes's backend into a CEL
  compiler milestone; it's its own project with its own issues once
  scoped.

## 3. Versioning and resolution — this is where §7's deferred question gets answered

`CEL_V2_LANGUAGE_SPEC.md` section 7 explicitly deferred module
versioning ("always resolves to the asset's current state, no
versioning" as the v1 answer for same-project/local imports). Hermes is
where real versioning lives: a project references a module by name and
a version or version range from the store (Cargo-style semantic
versioning: exact pin, `^1.2`-style range, etc.), Hermes resolves the
full dependency graph, and a **lockfile** records the exact resolved
versions actually used — so a project builds identically every time
until someone deliberately updates it. Local (non-store) module imports
from `CEL_V2_LANGUAGE_SPEC.md` section 7 keep their own simpler,
unversioned behavior; Hermes-sourced dependencies are the versioned
case.

## 4. Local cache and distribution format

A referenced module is downloaded once, cached locally (a Suite-wide
cache location — likely alongside the existing suite storage/bootstrap
system, not a per-project copy), and reused across every project that
depends on that exact version — same principle as Cargo's
`~/.cargo/registry` cache.

**Distribution format — flagging a real tension with the existing module
design rather than silently picking one:** the user asked for source, a
binary format, or an intermediate to all be possible. Two of those fit
cleanly with the already-decided "CEL stays one compiled unit, a module
is reusable addressable source" model (`CEL_V2_LANGUAGE_SPEC.md` §6); one
does not:

- **Source** — fits cleanly. The default case, no tension.
- **Intermediate (serialized AST, parse skipped)** — fits cleanly. Still
  merged into the single whole-program compile at the consuming
  project's build time, just skips the lex/parse step. Reasonable
  reason to want this: faster iteration on a large module, or wanting to
  distribute without exposing raw source formatting/comments.
- **Genuine binary (pre-compiled native code, loaded as a separate
  artifact rather than merged into the whole-program JIT pass)** — this
  is NOT a small addition. It breaks the single-compilation-unit model
  this whole design has leaned on (§0 of the language spec: CEL's whole
  premise is compiling everything together in under a second, so
  separate compilation solves a problem CEL doesn't have). A real binary
  distribution format would mean a second execution model (dynamically
  loaded plugin code with a fixed ABI entry point, resolved at runtime
  rather than JIT'd as part of the program) living alongside the normal
  one. Recommend: build source + intermediate-AST distribution first: revisit
  true binary distribution only if a concrete case shows up where even
  IR-gen/optimization time (not parsing) is the actual bottleneck —
  given CEL's sub-second full-rebuild goal, that's not obviously a real
  problem yet.

## 5. Open questions, not decided here

- Exact manifest format for a project's Hermes dependencies (own file,
  or fields on the project's existing Suite project manifest?) — likely
  the latter, reusing existing Suite project-manifest infrastructure
  rather than inventing a parallel one, matching tonight's recurring
  "reuse the Suite's existing systems" pattern, but not decided.
  Similarly: does a published module's metadata reuse `celModule` asset
  metadata directly, or does Hermes need its own richer schema (author,
  license, changelog) layered on top?
- Authentication/publish permissions model for LagDaemon.com — who can
  publish, is there any review/moderation, namespacing of publisher
  identity (e.g. `wwestlake/vecutils` vs a flat global name).
- Offline behavior — what happens when the store is unreachable and a
  project depends on a version not already in the local cache.

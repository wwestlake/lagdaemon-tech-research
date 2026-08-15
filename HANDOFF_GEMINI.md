# Handoff Doc — LagDaemon Tech Research / Frust / Frate

Written by Claude for Gemini (or whoever picks this up next). Claude is
handing off because of a weekly usage cap, not because the work is done.
Read this fully before touching code — it links two repos and a live
production system.

## The two repos involved

1. **`D:\000 Tech Research`** (this repo, primary working directory) —
   a from-scratch JUCE C++ IDE + a new language ("Frust") + its package
   manager ("Frate"). Experimental/research, no boss, no deadline — the
   user treats this as a genuine "let's build cool things" project.
2. **`D:\000 LLM Data System\djehuti`** — a REAL, deployed production
   system: `lagdaemon.com` (F#/.NET API + React dashboard + PostgreSQL +
   S3, deployed via GitHub Actions). Not a toy. **Before touching this repo,
   read `djehuti/AGENTS.md` in full and follow it exactly** — it has hard
   rules about migrations (Claude/Gemini runs them via SSH, never asks the
   user), branching (`feature branch → develop → PR to main`, never push
   straight to main, never merge your own develop→main PR), and
   communication style. It is the authoritative source of project rules,
   overriding anything remembered from a prior session.

## Current state, subsystem by subsystem

### `projects/02_juce_language_host` — the IDE ("LagDaemon Language Research IDE")
Working, builds, runs. Custom (non-`TabbedComponent`) docking system with
resizable splitters and persisted layout (`%APPDATA%\LagDaemonResearchIDE\layout.json`).
Panels: file tree, real syntax-highlighted code editor (`CodeEditorComponent`
based, custom `FrustTokeniser`), a REPL/console panel with JSON
save/load of variable bindings, a "Frust Context" variable inspector
(sortable/searchable table), an OpenAI-backed AI Assistant chat panel
(multi-profile key config at `%APPDATA%\LagDaemonResearchIDE\ai_config.json`,
keys never touch chat history), and a working File menu (New/Open
Folder/Save/Save As/Close/Exit) plus a Run button that executes the active
editor tab's content in the REPL.

### `projects/01_language_paradigms/02_functional` — Frust, the language
Real flex/bison (WinFlexBison at `D:/tools/winflexbison`) LALR(1) grammar
(`grammar/frust.l`, `grammar/frust.y`), arena-owned flat-struct AST
(`AST.h`), LLVM/OrcJIT backend (`Codegen.h`) — actually JIT-compiles and
runs Frust code, not a toy interpreter. `frust.y` has `%expect 3` as a
documented, verified invariant — if that count ever changes unexpectedly,
something broke; don't just bump the number, find out why.
`FRUST_LANG_SPEC.md` is the full language spec and doubles as the AI
Assistant panel's system prompt — keep them in sync if the language changes.
Supports linking libc/stdlib; no `use pod::thing` import syntax yet (see
Frate below); no AOT/"hard iron mode" compilation yet — both explicitly
deferred by the user, not oversights.

### `projects/05_frate` — Frate, the package manager (READ `FRATE_SPEC.md` FIRST)
**This is the most likely next task.** `FRATE_SPEC.md` is the authoritative
spec — file formats (`pod.json`, `frate.json`, `.frpod` zip packages), local
cache layout, resolution algorithm, and the registry HTTP contract. It was
written and agreed with the user before any client code existed.

- **Server side: DONE and LIVE.** Shipped this session as new endpoints on
  djehuti (not a new server — see FRATE_SPEC.md section 6 for the exact
  contract). Verified working in production:
  - `GET https://lagdaemon.com/djehuti/api/frate/pods?q=` — public search
  - `GET https://lagdaemon.com/djehuti/api/frate/pods/{name}/{version}` —
    public download, 302 to a presigned S3 URL
  - `POST .../frate/pods/{name}/{version}/upload-url` and
    `POST .../frate/pods/{name}/{version}` — publish flow, requires the
    `frate`/`publisher` context role (granted via
    `Permissions.grantContextRole` — no self-service UI yet, an admin has
    to run it directly)
  - Backing code: `djehuti/src/Djehuti.Api/FratePodRepository.fs`,
    migration 82 (`djehuti/migrations/frate_registry.sql`, already applied
    to prod), `Permissions.ModuleFrate`/`RolePublisher`, four endpoints in
    `Program.fs` near the Media section.
  - **Open item, not yet resolved with the user:** the license allow-list
    (`FratePodRepository.allowedLicenses`) is a placeholder set of common
    OSS licenses (MIT, Apache-2.0, BSD-2/3-Clause, ISC, MPL-2.0,
    GPL/LGPL-3.0, Unlicense, CC0-1.0). The user said they'd define the real
    list separately — that conversation hasn't happened yet.
- **Client side: NOT STARTED.** `CMakeLists.txt` in `projects/05_frate/`
  already declares the `frate_lib` target and lists the header/source files
  it expects (`FrateConfig`, `FrateCache`, `FrateRegistryClient`,
  `FrateResolver`, `FratePodBuilder`, `PodMetadata`) — **none of those
  `.h`/`.cpp` files exist yet.** This is the concrete next step: implement
  them per `FRATE_SPEC.md` sections 3–7, then wire a "Frate" dock panel
  into the IDE per section 8. `use pod::thing` import syntax in Frust
  itself is explicitly out of scope for this pass (needs real grammar work
  first) — Frate should just make pods resolvable data on disk.

## Toolchain (installed, verified — don't re-probe)
LLVM, JUCE, CMake, VS2022 are installed and working. flex/bison ARE
installed, at `D:/tools/winflexbison` (not on PATH by default — that threw
off a `where`/`which` check earlier this project; the CMake config finds it
directly by path). Build output for every sub-project routes to shared
top-level `bin/Debug|Release` and `lib/Debug|Release` — verified working
(see `projects/02_juce_language_host/CMakeLists.txt` for the pattern to
copy into any new target).

## Working style the user has been explicit about
- **Build directly to a stated acceptance criterion.** If the user says
  "X should always do Y," implement that outcome directly — don't go into
  unrelated diagnostic detours first. (This came from a real episode where
  Claude chased the wrong root cause on an editor bug instead of just
  fixing what the user had already described.)
- This is an exploratory/fun project — "we don't have a boss." Reasonable
  to suggest something is cool and just build it, but confirm before large
  detours.
- For djehuti specifically: precise numbers not vague quantities, plain
  unambiguous language (user has a DoD background), always give clickable
  PR/URL links, respond in words before diving into tool calls.

## Git state right now
- **djehuti**: `develop` branch, PR #407 (Frate registry + some
  pre-existing unrelated develop commits) has been merged to `main` and
  deployed — confirmed live via curl against the endpoints above.
- **This repo (Tech Research)**: on `master`, **55 files with uncommitted
  changes** (modifications + deletions in
  `projects/01_language_paradigms/02_functional/`, new files under
  `grammar/`, IDE source changes, etc.) — nothing has been committed this
  session. Review `git status` / `git diff` before assuming what's actually
  saved vs. still working-tree-only.

## Suggested first move
Read `FRATE_SPEC.md` end to end, then `projects/05_frate/CMakeLists.txt`
to see the exact file list it already expects, then start with
`PodMetadata.h`/`PodMetadataJson.cpp` (parsing `pod.json`/`frate.json`) —
everything else builds on that.

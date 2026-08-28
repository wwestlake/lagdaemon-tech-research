# AGENTS.md

Rules for any AI agent (Claude, Codex, Gemini, etc.) working in this repo.

**Frust and its whole ecosystem (compiler, IDE, plugin host, node
compiler, stdlib, etc.) moved to its own repo, 2026-08-24:
[`wwestlake/FrustLang`](https://github.com/wwestlake/FrustLang)** -
full history preserved via `git filter-repo`. That repo has its own
`AGENTS.md` (carried over from this one). Frust work no longer happens
here - if you're looking for `projects/01_language_paradigms/02_functional`,
`projects/09_frust_plugin_host`, `projects/02_juce_language_host`, or
similar, they're gone from this repo and live there now.

## Platform target - read this first

**Windows only. No Linux/Unix/cross-platform consideration, ever, for
any part of this project — the language, the compiler, the IDE, the
standard library — unless the user explicitly asks for it in that
specific moment.** Do not pause, defer, or scope a feature down
because "it wouldn't be portable" or "there's no platform-conditional
compilation mechanism yet." Do not add `#ifdef`/`#cfg`-style
portability branches, do not write code with an eye toward a future
Linux port, do not note "Windows-only for now" as a caveat implying
it should change later. Build it for Windows and ship it.

The user has stated this directly, repeatedly, across sessions
("I have said this umpteen times... it does not need to be compatible
with unix, stop that, its not a consideration for you now" -
2026-08-22). Treat it as settled, not open for reconsideration. If you
notice yourself about to hedge on cross-platform grounds, that is the
signal to stop and just build the Windows version.

## Build

- **Debug builds, not Release.** Build/run with `--config Debug`, not `Release`, unless explicitly told otherwise for a specific test.
- **Single-core builds only.** Never allow parallel builds on this machine — this is the user's own machine and they need it usable while a build runs. You MUST explicitly pass `/m:1` to MSBuild every time, as the default may attempt to use all cores. Example: `MSBuild.exe solution.sln /t:target /p:Configuration=Debug /m:1`.

## Standard workflow (every change, no exceptions)

1. Make the change.
2. Build it (Debug config) and actually run/verify it — don't claim done on compile-success alone.
3. `git status` / `git diff` — check what's actually changed before staging.
4. Commit, with a real message explaining why, not just what.
5. Push to `origin` — for this repo the user has standing authorization to push routinely ("push early, push often"); still use judgment on force-push/branch-deletion style destructive ops, those always need explicit sign-off.
6. If it's a submodule, commit+push inside the submodule first, THEN commit+push the parent repo's pointer bump. Two separate pushes, always in that order.
7. If you told the user a rule/process applies going forward, write it into this file, not just into your own private memory. This file is what's authoritative and visible in the repo — private memory is a supplement, never a substitute.

## Git discipline

- **Commit AND push proactively, both, every time.** After every verified build/change: commit right then, push to `origin`/`master` right after. Don't wait to be asked for either one - "commit only when explicitly asked" is not and has never been a rule here (a prior version of this file said otherwise in this section while contradicting the Standard Workflow section above; that was wrong and has been corrected). The user has said this directly and repeatedly: "I have consistently said commit commit commit, you don't need my permission to commit code."
- **This repo has no deployment - it's just a git repo.** `master` is the de facto working branch (no separate dev branch exists here); pushing straight to it carries no CI/CD or production risk. Treat pushing to `master` as routine, not something to hesitate over.
- Never force-push, never `--no-verify`, never skip hooks without being told to.

## Communication

- **Don't silently downgrade a possibly-transient failure into "it's gone" before an expensive fallback.** Say the assumption out loud first, especially before hours-long rebuilds.
- **State the target machine/file directly instead of asking the user to disambiguate** when context already makes it obvious.
- **Don't ask the user to make implementation-level technical decisions** — make the call and proceed. Only surface real goals/priorities as questions.
- **Discuss real design choices before implementing them** — don't just build and present as done.
- **Critical handoff info (passwords, IPs, key facts) gets its own short, clearly labeled block** — never buried in a paragraph.
- **Never trigger UAC/sudo/admin elevation beyond already-agreed scope without discussing first**, every time, no exceptions.

## Credentials

- Any new credential established in a session (SSH key, API key, host access) goes into `CREDENTIALS-MASTER.txt` (gitignored, repo root) immediately — not just used in-session and forgotten.
- Never echo that file's actual secret values back into chat or into any other file.

## SSH

- Before the first `ssh` to a host in a session, check `~/.ssh/` for the right identity key and use it explicitly with `-o BatchMode=yes`. Don't let a bare/failed attempt fall through to an interactive password prompt.

## Toolchain

- Check `dev_toolchain_capabilities` state before re-running `which`/`where` discovery commands — don't re-probe what's already known.

# AGENTS.md

Rules for any AI agent (Claude, Codex, Gemini, etc.) working in this repo.

## Build

- **Debug builds, not Release.** Build/run with `--config Debug`, not `Release`, unless explicitly told otherwise for a specific test.

## Standard workflow (every change, no exceptions)

1. Make the change.
2. Build it (Debug config) and actually run/verify it — don't claim done on compile-success alone.
3. `git status` / `git diff` — check what's actually changed before staging.
4. Commit, with a real message explaining why, not just what.
5. Push to `origin` — for this repo the user has standing authorization to push routinely ("push early, push often"); still use judgment on force-push/branch-deletion style destructive ops, those always need explicit sign-off.
6. If it's a submodule (e.g. `projects/06_frust_library`), commit+push inside the submodule first, THEN commit+push the parent repo's pointer bump. Two separate pushes, always in that order.
7. If you told the user a rule/process applies going forward, write it into this file, not just into your own private memory. This file is what's authoritative and visible in the repo — private memory is a supplement, never a substitute.

## Git discipline

- **Commit often.** Commit after every verified build/change, don't let work pile up uncommitted.
- **Push needs explicit go-ahead each time.** Committing is proactive; pushing to `origin` is not, unless the user has already said so for this specific batch.
- Never force-push, never `--no-verify`, never skip hooks without being told to.

## Communication

- **Don't silently downgrade a possibly-transient failure into "it's gone" before an expensive fallback.** Say the assumption out loud first, especially before hours-long rebuilds.
- **State the target machine/file directly instead of asking the user to disambiguate** when context already makes it obvious.
- **Don't ask the user to make implementation-level technical decisions** — make the call and proceed. Only surface real goals/priorities as questions.
- **Discuss real design choices before implementing them** — especially Frust language/tooling conventions — don't just build and present as done.
- **Critical handoff info (passwords, IPs, key facts) gets its own short, clearly labeled block** — never buried in a paragraph.
- **Never trigger UAC/sudo/admin elevation beyond already-agreed scope without discussing first**, every time, no exceptions.

## Credentials

- Any new credential established in a session (SSH key, API key, host access) goes into `CREDENTIALS-MASTER.txt` (gitignored, repo root) immediately — not just used in-session and forgotten.
- Never echo that file's actual secret values back into chat or into any other file.

## SSH

- Before the first `ssh` to a host in a session, check `~/.ssh/` for the right identity key and use it explicitly with `-o BatchMode=yes`. Don't let a bare/failed attempt fall through to an interactive password prompt.

## Toolchain

- Check `dev_toolchain_capabilities` state before re-running `which`/`where` discovery commands — don't re-probe what's already known.

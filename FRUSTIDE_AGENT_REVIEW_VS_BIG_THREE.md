# FrustIDE Agent: Review Against Claude Code, Codex and Gemini CLI

Written by Claude (the Creation Suite agent) for the owner and for research, 2026-09-22. Read-only review: nothing in
`D:\FrustLang` was changed.

**What was reviewed:** the research IDE at `D:\FrustLang`, branch `claude/disk-separation`, commit `3acecae`:
`projects/02_juce_language_host/Source/AgentTask.{h,cpp}`, `EngineerTools.{h,cpp}` and the run loop in `AiChatPanel.cpp`,
plus the briefing `FRUSTIDE_AGENTIC_ASSISTANT_BRIEFING_FOR_CLAUDE.md`.

**What it was compared with:** Anthropic's Claude Code, OpenAI's Codex CLI and Google's Gemini CLI. Claude Code's
permission model was checked against its current documentation today. Codex and Gemini CLI are described from their
documentation where it could be fetched and otherwise from what I know of them; details can differ between versions.

**The question behind it:** the owner wants FrustIDE to be a real IDE, not a FRust-only one. The model already knows C++,
C#/.NET, Rust and Python; it needs the compilers, not lessons.

---

## 1. The one-line finding

FrustIDE's **task discipline is stronger than the big three's**, but its **reach is much smaller**: it has no way to run a
command. Every one of the big three gives the model a shell (confined and approved), and that is how they compile and test
any language. Adding that, carefully, is what turns FrustIDE from a FRust IDE into an IDE.

---

## 2. Side by side

| | FrustIDE today | Claude Code | Codex CLI | Gemini CLI |
|---|---|---|---|---|
| Read / search files | `workspace_list/read/search` | Read, Glob, Grep | shell (`rg`, `cat`) | read_file, glob, search_file_content |
| Edit files | create / write (SHA-checked) / exact replace | Write, Edit (exact, unique string) | `apply_patch` (diff) | write_file, replace |
| Run commands | **none** | Bash (sandboxed, approved) | shell (sandboxed, approval policy) | run_shell_command (approved, optional sandbox) |
| Compile / test | FRust only, in memory | through Bash | through shell | through shell |
| Plan | `agent_set_plan` (steps, no status) | TodoWrite (each step pending / in progress / done) | `update_plan` (with status) | (plan via prompts; checkpoints) |
| Finish | **host gate: rejected until inspected, planned, changed, verified** | model stops; optional "stop" hooks can block | model stops | model stops |
| Ask the user | `agent_request_user` (rejected before inspecting) | asks in chat | asks in chat | asks in chat |
| Who decides a write is allowed | keyword guess on the user's words, plus an Access ceiling | per-action permission: allow / ask / deny rules, modes | sandbox mode + approval policy | confirmation per write/command, "YOLO" to skip |
| Undo | none | checkpoints / rewind | (git) | checkpointing (shadow snapshots) |
| Project instructions | none | `CLAUDE.md` | `AGENTS.md` | `GEMINI.md` |
| Long tasks | hard stop at 24 rounds | automatic context compaction | automatic compaction | compression |
| Web | hosted web search | WebSearch, WebFetch | web search | google_web_search, web_fetch |
| Conversation record | **SHA-256 hash chain** | plain transcript | plain transcript | plain transcript |

---

## 3. What FrustIDE already does well (keep it)

1. **The host owns completion.** None of the big three refuses a "done" that has not been earned; FrustIDE does. That is
   the idea in the research screenshot and it is correct. Keep it; generalize the evidence (section 4.2).
2. **Edits are safe by construction.** A write must match the SHA-256 of the latest read, and a replace must match exactly
   one place. That is the same protection Claude Code's Edit tool gives, and stronger than a blind write.
3. **Paths are confined** to the open project (canonicalized, checked against the root).
4. **The Responses API is used properly:** prior output items are carried between rounds, so reasoning and hosted-tool
   state survive; required tool choice during a task; retries honour `Retry-After`.
5. **Tamper-evident conversations.** None of the big three has this.

---

## 4. What to change, in order

### 4.1 A way to run the compilers (the heart of "not FRust-only")

Two tools, both confined to the project folder:

- **`build` / `test`: a structured tool with a plug-in per toolchain.** The model says "build" or "test"; the host picks the
  toolchain from the project (a `CMakeLists.txt`, a `.sln`/`.csproj`, `Cargo.toml`, a `frate.toml`...) and runs it the
  approved way. First plug-ins: **C++ with CMake + MSVC**, **.NET with `dotnet build` / `dotnet test`**, and FRust as today.
  Each plug-in turns its compiler's messages into `file:line:column: message`, so the model repairs C++ and C# the same way
  it repairs FRust now. The host detects which toolchains are installed (Visual Studio's compilers through `vswhere`, the
  .NET SDK) and offers only those.
- **`run_command`: a general command tool, for everything else** (a script, `git status`, a code generator). This is what
  the big three have.

The owner's machine rules go into the host, not the prompt, so no model can break them:

- one build at a time on this machine (a queue);
- single-core (`/m:1`, `-j1`) and always an explicit target;
- never build, rebuild or reinstall LLVM, and never run vcpkg in manifest mode on a manifest that lists LLVM;
- a time limit per command, and output cut to its head and tail (compiler logs can be huge) with the full log kept in a file.

**The caution:** a build is not read-only. CMake scripts, MSBuild targets and `build.rs` run code. So `build`, `test` and
`run_command` need their own permission, decided by the user (4.3), not granted by a keyword.

### 4.2 Verification that is not only FRust

Today "verified" means `workspace_check_frust` passed. Make it "the project's own checks passed after the last change":
the build for the project's toolchain, and its tests when it has any. The completion gate then works for a C++ or .NET task
exactly as it does for FRust. Record which check ran and its result in the task state, as now.

### 4.3 Stop guessing permission from the user's words

`mutationIntentFor` decides whether the model may write by looking for words like "make" or "fix", and treats "we are
building..." as status. That is brittle both ways: it already let a status message create
`D:\000 FrustMUD\src\registry_tool.fr`, and it will refuse real requests phrased differently. The big three do not guess:

- the **mode** says what kind of work this is (Plan and Review never write; Execute does);
- **each action** that changes something or runs a command is **allowed, asked, or denied** by rules the user controls:
  "always allow `dotnet build` in this project", "ask before any write", "never run `rm`". The rules are saved per project.
- in "ask", the user sees the diff or the exact command before it runs.

Keep the Access ceiling (Observe / Workspace), and add the per-action rules under it.

### 4.4 Project instructions and undo

- Read **`AGENTS.md`** from the project root (and `CLAUDE.md` if present) into the run, as Codex and Claude Code do. The
  owner already keeps his rules there; FrustIDE would follow them too.
- **Checkpoint before every write** (a copy of the file under `.agent-state`), with a "rewind" command, as Claude Code and
  Gemini CLI offer. It makes Execute mode much less frightening.

### 4.5 Long tasks

Replace the hard 24-round stop with a budget and **compaction**: when the conversation grows large, the host asks for a
summary of progress, keeps the task state, and continues from the summary, as all three do. Keep a limit, but on progress
(no successful tool result for N rounds), not on round count.

### 4.6 Smaller things

- Plan steps with status (pending / in progress / done), shown in the IDE, as Codex and Claude Code do.
- A web-page fetch tool beside hosted search, treating the page as untrusted data.
- Later: subagents (a read-only researcher the main agent can send off), as Claude Code does.

---

## 5. Suggested order of work

1. `build` / `test` with the C++ (CMake/MSVC) and .NET plug-ins, the machine rules, output limits, and diagnostics parsing.
2. The general verification gate (4.2).
3. Per-action allow / ask / deny rules, replacing the keyword guess (4.3). Items 1 and 3 should land together: running
   commands without the approval model would be the dangerous half on its own.
4. `run_command` under those rules.
5. `AGENTS.md` and checkpoints (4.4).
6. Compaction (4.5).

Then back to the network pods (`frust_net` TCP, text parsing), per `FRUST_MUD_REQUIRED_PODS.md`.

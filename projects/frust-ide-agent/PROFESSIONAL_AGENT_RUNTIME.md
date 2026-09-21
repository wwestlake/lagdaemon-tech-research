# Professional Coding Agent Runtime

## Purpose

FrustIDE needs a coding agent, not a chatbot with file functions. The distinction is the runtime around the model. A professional agent receives an assigned goal, works in a host-controlled loop, observes real tool results, maintains explicit process state, and cannot report success until the host has validated the required evidence.

This design remains provider-neutral. The current OpenAI provider carries typed tool calls, but the task state, mode rules, permissions, completion gates, and persistence belong to FrustIDE.

## Research Basis

- OpenAI describes an agent run as a loop: call the model, execute requested tools, append results, and call the model again until final output or a stopping condition. Sessions preserve working context across turns. See [Running agents](https://openai.github.io/openai-agents-python/running_agents/).
- OpenAI's practical guide identifies the model, tools, and instructions as the foundation, with the run loop and guardrails owned by the orchestrating application. See [A practical guide to building agents](https://openai.com/business/guides-and-resources/a-practical-guide-to-building-ai-agents/).
- Anthropic distinguishes a chatbot from an agent by the self-directed plan-act-observe-adjust loop and emphasizes ground truth from the environment, checkpoints, and stopping conditions. See [Building effective agents](https://www.anthropic.com/engineering/building-effective-agents).
- ReAct demonstrates why reasoning and environment actions must be interleaved: observations update the plan and reduce hallucination and error propagation. See [ReAct](https://arxiv.org/abs/2210.03629).

## Runtime Responsibilities

The model decides how to pursue a task. FrustIDE owns the facts that must not depend on model memory or honesty:

1. Current goal and operating mode.
2. Permission ceiling and project root.
3. Current process phase.
4. Whether inspection actually occurred.
5. Whether a write actually succeeded.
6. Whether the latest changed Frust source actually passed the compiler.
7. Whether completion is allowed.
8. Durable state across conversation turns and application restarts.

The host reconstructs and injects a compact state packet after every tool result. Conversation prose is not authoritative process state.

## Modes

Mode describes the job. Access describes the maximum permission. They are deliberately separate.

| Mode | Behavior | Writes |
|---|---|---|
| `Auto` | Answers ordinary questions directly; promotes implementation requests to a full Execute run. | Only with Workspace access |
| `Plan` | Inspects the project and records a durable concrete plan. | Never |
| `Execute` | Inspects, plans, edits, verifies, repairs, and completes. | With Workspace access |
| `Review` | Inspects existing work and reports findings. | Never |

A completed Plan can seed a later Execute run when the user says `do it`, `go ahead`, `proceed`, `continue`, or explicitly asks to execute the plan.

## Execute State Machine

```text
assigned
   -> inspect
   -> plan
   -> implement
   -> verify
   -> finish
   -> completed

Any active phase -> waiting-for-user, only for a real external blocker
Any active phase -> failed, on provider failure or the bounded turn limit
```

The task is stored as JSON under the conversation folder's `.agent-state` directory. It is separate from the hash-chained conversation because it is mutable runtime state, not conversation evidence.

## Completion Contract

For a Frust coding task, `agent_complete_task` is rejected unless all of these facts are true:

- at least one project inspection tool succeeded;
- an explicit plan was recorded;
- at least one workspace change succeeded;
- the current changed source passed `workspace_check_frust`.

Every successful write clears prior verification. The model must check again after the latest change. A prose claim, code block, or proposed patch satisfies none of these conditions.

Plan mode requires inspection plus a recorded plan. Review mode requires inspection. Neither mode receives write tools even when the user's access ceiling is Workspace.

## Message Passing

Each model turn receives:

1. Mandatory FrustIDE behavior and permission rules.
2. The host-owned task packet: task ID, goal, mode, status, current phase, plan, and evidence flags.
3. Selective LiteSemRAG context.
4. Recent conversation messages.
5. The newest user request.

After a tool call, FrustIDE appends the exact tool result and a newly rendered task packet before asking the model to continue. Coding runs require tool calls until the host accepts completion or a real blocker is recorded.

## Verification Tool

`workspace_check_frust` is an IDE agent tool, not a compiler change. It reads the selected project file and passes its text to the existing `frust::Compile` in-memory API with `emitObject = false`. It returns the existing structured diagnostics. It creates no temporary source or object files and changes no grammar, parser, code generation, or compiler behavior.

## Current Scope And Next Tools

The first runtime is intentionally single-agent. Research consistently recommends a simple, observable agent loop before adding delegation. The next capability work should expand the typed tool surface in this order:

1. project-wide Frate build/check with structured diagnostics;
2. patch/diff preview and approval policy;
3. test execution and result capture;
4. move/rename and controlled delete;
5. Git diff, stage, commit, and push under separate permissions;
6. evaluator pass for larger changes;
7. specialist subagents only when measured tasks demonstrate a benefit.

The runtime should be evaluated from saved trajectories: task success, premature completion attempts, tool failures, repair iterations, verification rate, user interventions, token use, and wall time.

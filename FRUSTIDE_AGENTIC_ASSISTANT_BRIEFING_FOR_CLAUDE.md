# FrustIDE Agentic Assistant Briefing for Claude

## Purpose

This document hands off the current FrustIDE assistant architecture, its tools, and the protocols used to run an OpenAI model as a host-controlled coding agent. The implementation described here is in `D:\FrustLang` on branch `claude/disk-separation`. The main integration is commit `daab51b` (`Expand FrustIDE assistant integration`), followed by commit `79dc312` (`Throttle OpenAI requests proactively`).

The Debug executable is:

```text
D:\FrustLang\bin\Debug\frust_ide.exe
```

The IDE was built and tested successfully and was left stopped.

## Architecture at a Glance

The model is not trusted to declare that work happened. FrustIDE owns the task state, controls which tools are exposed, executes tool calls, records results, verifies required conditions, and decides when a task may complete.

The main layers are:

1. `AiChatPanel`: conversation orchestration, mode/access selection, RAG injection, provider/tool loop, and UI updates.
2. `AgentTask`: host-owned goal, plan, phase, evidence, verification, completion, and user-question state.
3. `EngineerTools`: constrained project and registry operations.
4. `OpenAiProvider`: OpenAI Responses API transport, function-call protocol, hosted web search, retries, and citations.
5. `LiteSemRAG`: Frust grammar, specification, help, standard-library APIs, pods, and tool training cards.
6. `LocalAgentApi`: authenticated loopback transport through which another local agent can converse with the running embedded agent.
7. `ConversationStore`: append-only SHA-256 hash-chained JSON conversations.

## User Controls

The AI Assistant toolbar has two independent controls:

- Access: `Observe` or `Workspace`.
- Mode: `Auto`, `Plan`, `Execute`, or `Review`.

Access is a ceiling, not an instruction to write. Write tools are exposed only for a host-classified Execute request. Ordinary chat and status statements receive Observe tools even when the selected ceiling is Workspace.

Mode behavior:

- `Auto`: a direct mutation request starts an Execute task; questions remain conversational.
- `Plan`: inspects and records a plan without modifying files.
- `Execute`: performs a requested mutation or resumes an explicitly requested saved plan.
- `Review`: inspects and reports findings without modifying files.

The intent guard deliberately treats statements such as `we are building...` as status, not authorization. This was added after the embedded agent misread a status update and created `D:\000 FrustMUD\src\registry_tool.fr`.

## Engineering Tools

The canonical tool catalog and generated training cards are:

```text
D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOLS.json
D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOL_CARDS.jsonl
```

Tool implementation:

```text
D:\FrustLang\projects\02_juce_language_host\Source\EngineerTools.h
D:\FrustLang\projects\02_juce_language_host\Source\EngineerTools.cpp
```

Current project tools:

| Tool | Access | Purpose |
|---|---|---|
| `workspace_list` | Observe | List the open project. Required as the first project inspection step. |
| `workspace_read` | Observe | Read a UTF-8 file with line numbers and return its SHA-256 revision. |
| `workspace_search` | Observe | Search project text and return file/line matches. |
| `registry_search` | Observe | List or search the public Frate pod registry, including versions and exports. |
| `workspace_check_frust` | Observe | Compile-check in-memory Frust source through `CompilerApi`; no OS temporary source file. |
| `workspace_create_directory` | Workspace/Execute | Create a project directory. |
| `workspace_create_file` | Workspace/Execute | Create a new file without overwriting an existing one. |
| `workspace_write_file` | Workspace/Execute | Rewrite an existing file only when its expected SHA-256 matches the latest read. |
| `workspace_replace_text` | Workspace/Execute | Perform a focused exact-text replacement; rejects ambiguous matches by default. |

`registry_search` calls this fixed public endpoint directly from the IDE:

```http
GET https://lagdaemon.com/djehuti/api/frate/pods
GET https://lagdaemon.com/djehuti/api/frate/pods?q={name-substring}
```

No registry credentials are required for searches. The result contains `name`, `description`, `latestVersion`, `license`, and `exports`. The tool remains available even when no project folder is open.

The endpoint and server contract are also documented in:

```text
D:\FrustLang\projects\05_frate\FRATE_SPEC.md
D:\FrustLang\projects\05_frate\include\frate\FrateRegistryClient.h
D:\FrustLang\projects\05_frate\src\FrateRegistryClient.cpp
```

## Host-Owned Task Protocol

Task source:

```text
D:\FrustLang\projects\02_juce_language_host\Source\AgentTask.h
D:\FrustLang\projects\02_juce_language_host\Source\AgentTask.cpp
```

For an agent run, FrustIDE adds three host-control function tools:

### `agent_set_plan`

Arguments:

```json
{
  "goal": "Concrete task goal",
  "steps": ["Inspect...", "Change...", "Verify..."]
}
```

The host rejects a plan until the model has successfully inspected the project. A professional plan requires at least two concrete steps.

### `agent_complete_task`

Arguments:

```json
{
  "summary": "Concise description of completed work"
}
```

The host rejects completion until all task requirements have evidence:

- project inspection occurred;
- a plan exists when required;
- a workspace mutation occurred when required;
- Frust verification passed after the latest change when required.

### `agent_request_user`

Arguments:

```json
{
  "question": "The information or decision needed",
  "reason": "Why project inspection and tools cannot resolve it"
}
```

The host rejects premature questions before project inspection. The model is instructed to use this only for a genuine blocker or user judgment.

Task states are persisted next to conversations:

```text
<conversation-folder>\.agent-state\<conversation-id>.json
```

Important execution rules:

1. Begin with `workspace_list` on the actual open root.
2. Read relevant files before planning or writing.
3. Call `agent_set_plan` before a required write.
4. Make changes through host tools, not by printing proposed code in chat.
5. Run `workspace_check_frust` after Frust changes and repair failures.
6. Continue until `agent_complete_task` is accepted or `agent_request_user` records a real blocker.
7. The host currently limits a run to 24 provider/tool rounds.

## OpenAI Responses Protocol

Provider implementation:

```text
D:\FrustLang\projects\04_ai_provider\include\ai_provider\AiProvider.h
D:\FrustLang\projects\04_ai_provider\src\OpenAiProvider.cpp
```

FrustIDE uses:

```http
POST https://api.openai.com/v1/responses
```

It no longer uses `/v1/chat/completions` for tool work. This resolves models that reject function tools combined with reasoning settings on Chat Completions.

### Request mapping

- Ordinary system/user/assistant history becomes typed Responses `input` message items.
- Host function definitions use direct Responses tool objects: `type`, `name`, `description`, and `parameters`.
- A model function call is read from an output item with `type: function_call`.
- Tool results are sent back as `type: function_call_output` with the matching `call_id`.
- Exact prior provider output items are retained in `providerItemsJson` between rounds. This preserves reasoning items and hosted-tool state instead of reconstructing lossy assistant messages.
- Active agent tasks use required tool choice; ordinary conversations use automatic tool choice.

Conceptual tool round:

```json
{
  "model": "selected-model",
  "input": [
    { "role": "system", "content": "..." },
    { "role": "user", "content": "..." },
    {
      "type": "function_call_output",
      "call_id": "call_123",
      "output": "Host tool result"
    }
  ],
  "tools": [
    {
      "type": "function",
      "name": "workspace_read",
      "description": "...",
      "parameters": { "type": "object", "properties": {} }
    }
  ],
  "tool_choice": "required"
}
```

### Hosted web search

For requests that explicitly indicate web research, current information, online lookup, or search, the provider adds:

```json
{ "type": "web_search" }
```

It requests web-search source metadata, preserves hosted search items between rounds, and renders returned URL citations as Markdown links. During an agent task, a hosted web-search-only round is fed back into the task loop so the model must continue into project/control tools instead of stopping at a prose research answer.

### Rate limits and errors

Rate limiting is treated as a FrustIDE transport responsibility, not a user or model failure.

The provider now has a process-wide proactive limiter shared by all OpenAI provider instances. Its state is partitioned by a non-reversible process-local hash of API key plus selected model; raw API keys are never stored in limiter state.

For every successful or rejected response it reads OpenAI's live headers:

```text
x-ratelimit-limit-requests
x-ratelimit-remaining-requests
x-ratelimit-reset-requests
x-ratelimit-limit-tokens
x-ratelimit-remaining-tokens
x-ratelimit-reset-tokens
```

Before the next request, the limiter:

1. Estimates input tokens from serialized request size.
2. Adds the explicit output reservation.
3. Atomically reserves one request and the estimated token amount against the shared in-process budget.
4. Sleeps until the relevant server-reported reset time when either budget is insufficient.
5. Refreshes its estimate from the next real response headers.

Responses now set `max_output_tokens` to `8192`. Previously the API could reserve a large default output allowance on every rapid agent round; combined with repeatedly transmitted tool context, this could cross a 200,000 TPM bucket even when visible answers were short.

The reactive safety net remains:

- HTTP `429` and server `5xx` responses are retried up to three attempts.
- `Retry-After` is honored when supplied.
- Token-reset headers are used when `Retry-After` is absent.
- Messages such as `try again in 1.9s` are recognized.
- Backoff may wait up to 120 seconds instead of the former 10-second ceiling.
- After exhausted retries, the UI shows a concise rate-limit message instead of the raw account/organization payload.

The first request for a model cannot be proactively paced because no live headers have been observed yet. Separate applications using the same account also cannot share FrustIDE's in-memory reservation ledger; their consumption becomes visible through the next response headers. A `429` can therefore still occur on a cold start or an out-of-process race, but sustained FrustIDE agent loops should pace themselves before crossing the reported budget.

OpenAI reference documentation:

- Responses migration: `https://platform.openai.com/docs/guides/migrate-to-responses`
- Web search: `https://platform.openai.com/docs/guides/tools-web-search`

## Local API for Agent-to-Agent Conversation

Implementation and tests:

```text
D:\FrustLang\projects\02_juce_language_host\Source\LocalAgentApi.h
D:\FrustLang\projects\02_juce_language_host\Source\LocalAgentApi.cpp
D:\FrustLang\projects\02_juce_language_host\Source\LocalAgentApiTests.cpp
D:\FrustLang\projects\frust-ide-agent\LOCAL_AGENT_API.md
```

When FrustIDE runs, it opens an HTTP listener on a random `127.0.0.1` port and writes a per-run discovery document:

```text
%APPDATA%\LagDaemonResearchIDE\agent-api.json
```

The document contains:

```json
{
  "schema": "frustide-agent-api",
  "version": 1,
  "baseUrl": "http://127.0.0.1:<random-port>",
  "token": "<random-per-run-token>",
  "transport": "http"
}
```

Every request requires:

```http
Authorization: Bearer <token>
```

Endpoints:

```http
GET /v1/status
POST /v1/messages
GET /v1/requests/{requestId}
```

Message submission body:

```json
{ "content": "Inspect the current project and report its status." }
```

`POST /v1/messages` returns `202 Accepted` with a request ID. Polling returns `queued`, `running`, `completed`, or `failed`, plus `response` or `error` when terminal.

The API enters through `AiChatPanel::submitExternalMessage`, so it uses the same active conversation, selected model, mode, access ceiling, RAG context, task controller, and tools as a UI message. It cannot bypass permissions. It rejects a message while the assistant is busy or while the user has an unsent draft in the input box.

The listener is loopback-only, request bodies are capped at 1 MiB, the token changes each run, responses disable caching, and the discovery file is removed on clean shutdown. Test instances use a separate temporary discovery file so they cannot overwrite a running IDE's endpoint information.

## Conversation Storage and Display

Storage implementation:

```text
D:\FrustLang\projects\02_juce_language_host\Source\ConversationStore.h
D:\FrustLang\projects\02_juce_language_host\Source\ConversationStore.cpp
```

Default folders:

```text
<Documents>\LagDaemon Research IDE\Conversations
<Documents>\LagDaemon Research IDE\Conversation Archive
```

Both are user-configurable in the AI Assistant. Each conversation is JSON using schema `djehuti-conversation-chain`. Blocks include their index, timestamp, role, content, previous hash, and SHA-256 hash. This makes alterations detectable; it is hash chaining, not proof-of-work or encryption.

Display implementation:

```text
D:\FrustLang\projects\02_juce_language_host\Source\AiConversationView.h
D:\FrustLang\projects\02_juce_language_host\Source\AiConversationView.cpp
```

For coding tasks, the visible reply is the concise host-owned completion summary. Tool activity is stored in the assistant message using this internal marker:

```text
:::details Project activity (N steps)
...
:::
```

The custom conversation view renders that content behind a collapsed `> Project activity` disclosure control. The model is explicitly instructed not to repeat written files or code in the visible completion summary. Ordinary explanatory chats may still show Markdown and code snippets normally.

## LiteSemRAG and Frust Knowledge

Important paths:

```text
D:\FrustLang\tools\rag\litesemrag_builder.py
D:\FrustLang\tools\rag\build_engineer_tool_cards.py
D:\FrustLang\tools\rag\frust_knowledge.db
D:\FrustLang\projects\02_juce_language_host\Source\RAGQuery.cpp
D:\FrustLang\projects\frust-ide-agent\FRUST_AI_CONTEXT.md
D:\FrustLang\projects\01_language_paradigms\02_functional\FRUST_LANG_SPEC.md
D:\FrustLang\projects\01_language_paradigms\02_functional\grammar\frust.y
D:\FrustLang\projects\01_language_paradigms\02_functional\grammar\frust.l
D:\FrustLang\wiki\reference
```

The previous builder skipped the standard library because `projects\06_frust_library` is a Git submodule. The builder now enumerates tracked manifests inside recursive submodules and qualifies function/struct node IDs with their pod ID to prevent same-name symbols from overwriting one another.

The rebuilt database contains 662 nodes and was directly verified to include:

```text
11-Collections  -> wiki\reference\11-Collections.md
len             -> projects\06_frust_library\core\src\string.fr
println_str     -> projects\06_frust_library\core\src\console_io.fr
read_line       -> projects\06_frust_library\core\src\file_io.fr
```

`RAGQuery.cpp` adds retrieval hints for String, collections/Vector, console printing, and line input so questions about those facilities retrieve concrete library/reference entries rather than only broad language prose.

## Other UI Work in This Batch

Editor tabs now have a visible close `x` and a right-click `Close` command:

```text
D:\FrustLang\projects\02_juce_language_host\Source\EditorTabComponent.h
D:\FrustLang\projects\02_juce_language_host\Source\EditorTabComponent.cpp
```

## Build and Verification

Build command used:

```powershell
cmake --build D:\FrustLang\projects\02_juce_language_host\build `
  --config Debug `
  --target LocalAgentApiTests EngineerToolsTests AgentTaskTests ai_provider_tests LagDaemonResearchIDE `
  -- /m:1
```

Passing executables:

```text
D:\FrustLang\bin\Debug\LocalAgentApiTests.exe
D:\FrustLang\bin\Debug\EngineerToolsTests.exe
D:\FrustLang\bin\Debug\AgentTaskTests.exe
D:\FrustLang\projects\02_juce_language_host\build\ai_provider_build\Debug\ai_provider_tests.exe
```

Verified results:

```text
LocalAgentApiTests: all checks passed
EngineerToolsTests: all checks passed
AgentTaskTests: all checks passed
AiConfig: save, reload, and missing-profile checks passed.
```

The final IDE link succeeded:

```text
D:\FrustLang\bin\Debug\frust_ide.exe
```

## Recommended Next Work

1. Launch the newly built IDE and verify `%APPDATA%\LagDaemonResearchIDE\agent-api.json` appears.
2. Use the documented API to send a read-only handshake and have the embedded agent call `registry_search` for `net`.
3. Confirm the live reply appears both through API polling and in the AI Assistant conversation.
4. Exercise an Execute request and confirm project activity is collapsed while file changes and verification remain visible on demand.
5. Remove or deliberately retain the accidentally generated `D:\000 FrustMUD\src\registry_tool.fr`; it was intentionally not deleted during this work.
6. Design a Frust tool ABI above the trusted C++ kernel. Keep permissions, approvals, path confinement, process/network grants, and tool loading in C++; allow higher-level tools to be implemented as Frust modules once the host ABI is defined.

## FrustMUD Pod Findings from the Latest Agent Conversation

The latest saved conversation was reviewed at:

```text
C:\Users\wwestlake\OneDrive - SkyKick\Documents\LagDaemon Research IDE\Conversations\449d570680df492d9f00fd2bf50ec836.json
```

The embedded agent correctly found `frust_net` in the registry, but registry metadata proves only that a pod and export names were published. Direct source inspection found that the current implementations are prototypes:

```text
D:\FrustLang\projects\frust_net\src\tcp.fr
D:\FrustLang\projects\frust_net\src\udp.fr
D:\FrustLang\projects\frust_collections\src
D:\FrustLang\projects\frust_http\src\client.fr
D:\FrustLang\projects\frust_json\src\parser.fr
D:\FrustLang\projects\frust_json\src\emitter.fr
```

Specifically:

- `tcp_connect` returns an unconnected stream with file descriptor `-1`.
- `udp_bind` returns an unbound socket with file descriptor `-1`.
- `RingBuffer`, `SpscQueue`, and `HashMap` operations are placeholders.
- `http_get` always returns status `404`.
- `json_parse` always returns `Null`; `json_emit` returns `0`.

The MUD does not need a collection of additional nominal pods. It needs a small set of real capabilities:

1. Complete and republish `frust_net` with TCP client and server operations. Minimum API: listener creation, bind/listen, accept, connect, byte read, byte write, stream/listener close, and retrievable error/status values. Blocking single-client behavior is enough for v0.1.
2. Add a real text/command parsing surface. This can extend `core` or become `frust_text`. Minimum API: ASCII trim, lowercase/case-insensitive comparison, whitespace tokenization or split-first-word, prefix/equality checks, and safe substring/slice handling.
3. Use compiler-intrinsic `Vector<T>` for the first three-room prototype. Do not depend on the current `frust_collections` implementations until they are completed. A functional string-keyed map becomes useful later for command aliases, exits, objects, and player lookup.
4. Update the MUD's `core` dependency from its current `1.0.0` to the published/current `1.0.3`, after checking compatibility. `core` 1.0.3 exposes `println_str`, `read_line`, `len`, allocation, memory, and file primitives.
5. Defer `frust_http`, `frust_websocket`, and `frust_json` for MUD v0.1. They are not needed for the plain TCP line protocol and their current source is skeletal.

The detailed work order is:

```text
D:\000 Tech Research\FRUST_MUD_REQUIRED_PODS.md
```

## Handoff Principle

Do not turn model prose into authority. The model proposes tool calls; the host determines available capabilities, performs the operations, records evidence, enforces task state, and decides whether completion is valid. This boundary is the core of the current design.

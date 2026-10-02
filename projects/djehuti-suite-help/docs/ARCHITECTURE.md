# DjehutiSuite Help System Architecture

Status: research prototype

Date: 2026-09-20

Scope: technology-independent source model, synchronization, browser publication, video, LiteSemRAG + ISD ingestion, and the agent context system (policies, processes, active process state, context assembly)

## Executive decision

DjehutiSuite help should be maintained as small, typed JSON topics in source control. Those files are the reviewed human knowledge. A build step combines them with a machine-generated inventory of public application behavior and produces two read models:

1. a normal help catalog, context map, and search corpus;
2. block-level semantic cards for LiteSemRAG and the Virtual Engineer.

The application and help system meet at stable semantic `helpId` values, not UI labels, filenames, routes, or source line numbers. The application emits a behavior signature for each help-bearing feature. A topic records the signature it was reviewed against. A meaningful application change therefore makes documentation validation fail until the topic is reviewed and updated.

This arrangement gives DjehutiSuite one help source without forcing the help browser, database, or RAG index to share an implementation.

## Design goals

- A user can browse, search, deep-link, and open context-sensitive help normally.
- The Virtual Engineer receives small, typed, attributable semantic units rather than arbitrary text slices.
- A pull request cannot silently alter a documented behavior contract.
- Videos hosted on the DjehutiSuite website can be embedded or linked, chaptered, captioned, and understood through transcripts.
- Help remains usable without video and resilient if a media provider changes.
- Generated data can be rebuilt or reindexed without losing authored knowledge.
- The model remains independent of UI framework, database, search engine, and video host.

## What is authoritative

| Artifact | Authority | Edited by | Purpose |
|---|---|---|---|
| Topic JSON | Canonical | Writers and engineers | Explanations, tasks, recovery, media, relationships |
| Application help inventory | Canonical for exposed behavior | Application build | Stable IDs, ownership, behavior signatures, source references |
| JSON Schemas | Canonical contract | Help platform owners | Validate both inputs |
| Browser catalog and context map | Derived | Compiler | Rendering, navigation, search, deep links |
| Semantic cards | Derived | Compiler | LiteSemRAG ingestion |
| Database rows, embeddings, indexes | Derived | Ingestion pipeline | Query acceleration and graph traversal |
| Analytics and feedback | Evidence only | Runtime | Discover gaps; never overwrite content |

Embeddings, generated summaries, HTML, and database records must not be committed as the source of truth. They are rebuildable projections.

## Topic model

The topic type follows the useful separation popularized by Diátaxis: tutorial, how-to, reference, and explanation, with troubleshooting promoted to an explicit fifth type. This also aligns with DITA's long-standing distinction between concept, task, and reference topics. A task should answer a concrete "How do I?" question and include prerequisites, steps, expected results, and recovery where applicable.

Each topic contains:

- a stable topic ID and locale;
- type, title, short summary, product, status, owners, and applicability;
- context bindings to application `helpId` values;
- search keywords and real alternative user questions;
- explicit graph relations to other topics;
- small, stable content blocks;
- review revision, reviewer, date, source revision, and evidence.

Content blocks are structural rather than a single HTML field. The initial vocabulary includes paragraphs, steps, notes, warnings, code, images, videos, definitions, and troubleshooting. Text may use a restricted CommonMark subset, but raw HTML and executable content should be rejected or sanitized by the renderer.

Stable block IDs matter. They provide durable deep links, video-chapter targets, analytics locations, and deterministic RAG chunk identities such as:

```text
example.djehuti.help.context-help#open-context-help
```

The topic file is the natural review unit; the block is the natural retrieval unit.

## Application inventory

The inventory must be generated from the same registries or descriptors that define public commands, views, settings, workflows, and errors. It should not be assembled by scraping screenshots, UI trees, or source text.

Each inventory item includes:

- `helpId`: immutable semantic identity;
- `kind`: command, view, control, workflow, setting, error, or application;
- current label and optional UI path or route;
- owning team;
- documentation policy: required, recommended, or none with a reason;
- source reference for maintainers;
- behavior signature.

### Stable ID rules

Use names based on meaning, not placement:

```text
djehuti.station.timeline.split-clip
djehuti.station.export.frame-rate
djehuti.account.error.session-expired
```

Do not encode menu order, control type, keyboard shortcut, class name, or localized label. A feature can move from menu to toolbar without changing its identity. If meaning changes, create a new ID and retain an alias or deprecation record for the old one.

### Behavior signatures

The application build computes a SHA-256 digest over a normalized public behavior descriptor, for example:

```json
{
  "inputs": [{"name": "frameRate", "type": "rational", "required": true}],
  "defaults": {},
  "results": ["export-job-created"],
  "errors": ["invalid-frame-rate", "destination-unavailable"],
  "sideEffects": ["writes-media-file"]
}
```

Only user-observable contract fields belong in this descriptor. Source paths, labels, styling, and implementation classes do not. Canonical JSON serialization makes field ordering irrelevant before hashing.

The topic's context binding stores `verifiedBehaviorSignature`. If the contract changes, the inventory hash changes and CI reports stale documentation. Reviewing the behavior and retaining correct prose still requires updating the recorded signature; that small act is the review acknowledgment.

This is stronger than checking file modification times and less noisy than marking every code change as a documentation change.

## Synchronization workflow

### During implementation

1. The engineer adds or modifies the application's public feature descriptor.
2. The application build regenerates `help-inventory.json`.
3. The help compiler validates schemas and cross-references.
4. Required new IDs without topics fail CI.
5. Changed behavior signatures fail CI until affected topics are reviewed.
6. Topic and application owners review the same change.
7. Publication compiles browser and semantic outputs from the accepted sources.

### Checks enforced by the compiler

- Schema validity under JSON Schema Draft 2020-12.
- Unique topic IDs, block IDs, step IDs, and inventory help IDs.
- Required inventory coverage.
- Context IDs that no longer exist in the application.
- Stale behavior signatures.
- Broken topic relations and block anchors.
- Video records without captions or a transcript.
- HTTPS-only media locations and WebVTT caption naming.
- Explicit replacement for deprecated topics.

Production checks should add network link validation, media-host allowlists, version-range evaluation, localization parity, vocabulary linting, and accessibility review. Network checks should use caching and retry classification so a temporary website failure does not masquerade as a content defect.

### Ownership and pull requests

Place schemas, inventory adapters, and topic namespaces under code-owner review. GitHub can require code-owner approval before merge. Ownership should be assigned by product area rather than to one global documentation gatekeeper.

A pull request report should show:

- newly required and newly covered help IDs;
- stale topics caused by behavior changes;
- removed or deprecated contexts;
- changed topics and affected browser routes;
- semantic cards that will be inserted, updated, or tombstoned;
- accessibility or link warnings.

The report is the visible documentation debt ledger. Missing help must not disappear into a build log.

## Browser read model

The prototype emits a `help-catalog.json` containing full topics and normalized search documents plus a small `context-map.json`:

```json
{
  "contexts": {
    "djehuti.station.timeline.split-clip": {
      "topicId": "djehuti.station.timeline.split-clip",
      "anchorBlockId": "steps"
    }
  }
}
```

A browser implementation should provide:

- hierarchical browse views assembled from maps or product navigation configuration;
- full-text search over title, summary, keywords, alternative questions, and block text;
- stable URLs of the form `/help/{topicId}#{blockId}`;
- version, platform, edition, and feature-flag filtering;
- breadcrumbs and typed related-topic links;
- print-friendly rendering;
- accessible keyboard navigation, headings, landmarks, images, and media controls;
- a visible "applies to" statement when content is conditional;
- feedback tied to topic/block/revision, stored separately from authored content.

Navigation is not embedded as parent/child fields in every topic. A separate map can arrange the same topic differently for product, role, workflow, or website section without copying it. DITA uses this same separation between self-contained topics and maps that organize their relationships.

## Video design

A video block points to the canonical DjehutiSuite website page. The raw media URL or provider embed is optional and never the identity of the help item. The record may include:

- canonical page URL;
- allowlisted embed URL;
- thumbnail and duration;
- WebVTT captions;
- an HTML or text transcript;
- chapter start times and titles;
- related topic block IDs.

WebVTT is suitable because it supports captions, subtitles, chapters, and timed metadata. Chapters can connect a moment in the video directly to the matching task step or explanation block.

The transcript and chapter labels are ingested as text for the Virtual Engineer. The video pixels are not. The help browser may embed the video, while offline or restricted environments show the poster, description, transcript, and canonical page link.

Every instructional video requires captions or a transcript at schema level; production policy should normally require both. Instructions essential to completing a task must also exist as text. Important visual information needs an audio description or a descriptive transcript. This keeps the help usable for accessibility, search, constrained networks, and machine understanding.

Website publication can additionally expose `VideoObject` structured data, but that is a website projection and does not need to pollute the core help schema.

## LiteSemRAG ingestion

The existing LiteSemRAG research describes a small semantic graph rather than arbitrary document chunking. The compiler therefore creates deterministic cards:

- one topic summary card;
- one card per stable content block;
- card identity `{topicId}#{blockId}`;
- content hash over canonical block JSON;
- exact tokens from titles, keywords, IDs, error codes, and symbols;
- semantic text for later embedding;
- entity, context, and topic-relation edges;
- media transcript and chapter references.

An ingestion service should normalize these into separate records:

```text
Topic -> Block -> SemanticCard
  |        |          |
  |        |          +-- embedding / semantic neighborhood
  |        +------------- media / evidence / exact-token index
  +---------------------- context / relation / ownership graph
```

Use block content hashes for incremental updates. Unchanged cards retain embeddings. Removed blocks produce tombstones so old cards cannot remain retrievable. A topic status of `deprecated` remains queryable for historical diagnosis but should rank below its replacement for current versions.

### Retrieval order

1. Resolve exact context, error, command, symbol, and topic IDs.
2. Apply product/version/platform eligibility filters.
3. Traverse explicit prerequisite, reference, and troubleshooting edges.
4. Use lexical and semantic retrieval within the eligible set.
5. Rank verified current material above proposed or deprecated material.
6. Return source topic, block, revision, and evidence with every answer.

Exact identifiers must beat semantic similarity. This protects the Virtual Engineer from confidently selecting a nearby but wrong command or version.

## ISD integration

The local ISD research treats LiteSemRAG as terrain and ISD as query-time navigational instrumentation. Keep that boundary:

- reference drift pulls retrieval back toward the active task's context and entities;
- torsional resistance can trigger isolated semantic recovery when retrieval is trapped in an irrelevant neighborhood;
- high geodesic curvature identifies a topic pivot, lowers stale conversational weighting, and broadens retrieval around the new anchor.

ISD may alter retrieval weights, traversal breadth, and recovery strategy. It must not rewrite topic JSON, invent evidence, or silently change verification status. The result should remain auditable back to topic/block IDs and revisions.

## Versioning and localization

`schemaVersion` controls structural compatibility and should use explicit migrations. Product applicability is separate and belongs in `appliesTo`.

Recommended layout:

```text
content/
  en-US/
    station/
      timeline/
        split-clip.json
  fr-FR/
    station/
      timeline/
        split-clip.json
```

Localized topics retain the same semantic topic and block IDs but differ by locale. Translation tooling can compare `contentRevision` and source hashes to identify stale translations. Machine translation may produce a draft, but it must not mark itself verified.

For released products, store help with the source revision that built the release. The website can publish multiple versions while the in-application browser requests the version matching the running build.

## Security and privacy

- Treat topic text and transcripts as untrusted input at render time.
- Render a restricted markup subset and prohibit scripts, event handlers, and arbitrary iframe origins.
- Allowlist video and image hosts; use a restrictive content security policy.
- Do not place secrets, customer data, telemetry samples, or private URLs in help JSON.
- Fetch external media server-side only through controlled link checking, with size and timeout limits.
- Record feedback and search telemetry separately, with data minimization and retention rules.
- Never let retrieval-generated text modify canonical help without human review.

## Quality standard

A verified task topic should pass this editorial checklist:

- The title is a user goal, not an internal component name.
- The summary states the outcome and scope.
- Prerequisites are explicit only when necessary.
- Each step contains one action and, where useful, an observable result.
- UI labels come from the inventory or a generated token rather than being duplicated loosely.
- Common failure symptoms link to recovery guidance.
- Concepts explain why; reference topics enumerate facts; task prose stays procedural.
- Screenshots have useful alternative text and are not the only location of a value or instruction.
- Videos have captions, transcripts, chapters, and a complete text equivalent.
- Search terms include user language, known error text, and alternative questions.
- Every factual behavioral claim has source, test, manual-check, issue, or design evidence.
- The topic was checked against the behavior signature in the current inventory.

Quality analytics should track no-result searches, immediate query reformulation, repeated backtracking, unresolved troubleshooting, broken links, and outdated-version visits. Those are signals for editorial work, not automatic truth updates.

## Recommended rollout

### Phase 1: contract

- Approve namespaces and ownership boundaries.
- Add stable help IDs to one representative DjehutiSuite workflow.
- Generate inventory from its real command/view descriptors.
- Convert the research fixture into two real tasks, one reference topic, and one troubleshooting topic.
- Run the prototype compiler in CI as advisory.

### Phase 2: enforcement and browser

- Make required coverage and behavior-signature drift blocking.
- Build the browser against `help-catalog.json` and `context-map.json`.
- Add product navigation maps, version selection, local search, and website video allowlisting.
- Add code-owner rules and pull-request reporting.

### Phase 3: Virtual Engineer

- Ingest semantic cards with block-level hashes and tombstones.
- Add exact-ID, lexical, graph, and semantic retrieval in that order.
- Preserve evidence and revision provenance in answers.
- Apply ISD only to query-time retrieval steering and recovery.

### Phase 4: feedback loop

- Add privacy-reviewed help analytics and user feedback.
- Turn recurring failed searches into reviewed keywords, topics, or product fixes.
- Track documentation freshness and localization lag by owner and release.

## Rejected alternatives

### Markdown as the only canonical source

Markdown is pleasant to write but weak at enforcing context bindings, behavior signatures, typed relations, media accessibility, and stable block identities. Markdown remains useful inside constrained text fields or as an export.

### Database as the authoring source

A database makes change review, branching, release alignment, ownership, and reproducible builds harder. It should be a derived query model.

### Generate all prose from source code

Code can generate exact reference facts, labels, options, and defaults. It cannot reliably generate user goals, explanations, troubleshooting judgment, or good teaching. Generate facts; author understanding.

### Link help directly to UI labels or routes

Labels localize and routes move. Stable semantic IDs survive both.

### Store whole topics as one RAG chunk

Whole topics dilute retrieval and make evidence coarse. Arbitrary token windows sever procedural and relational meaning. Stable authored blocks provide the useful middle ground.

## Agent context system: policies, processes and process state

The help system above answers "what is this and how do I do it". The Virtual Engineer needs three more kinds of context that behave differently from help and must not be handled as help:

| Kind | What it is | Authority | Lifetime | Retrieved by |
|---|---|---|---|---|
| Help / product knowledge | Explanations, tasks, reference | Canonical, reviewed (Git) | Versioned with the product | Search and graph (existing design) |
| **Policy** | A rule the agent must follow | Canonical, reviewed (Git) | Versioned with the product | **Not retrieved.** Selected by deterministic code and injected |
| **Process** | A repeatable multi-step procedure | Canonical, reviewed (Git) | Versioned with the product | By ID once active; by search only to be *offered* |
| **Active process state** | Where one running instance of a process has got to | **Runtime data**, not authored | Created and ended at run time | Read by ID from the runtime store on every request |

This is an addition to the help architecture, not a replacement. Policies and processes reuse the same conventions: stable IDs, JSON Schema, review revisions with evidence, compiler validation, and derived read models. It is independent of any application, including the FRust IDE.

### Policy records

A policy is one rule, written as an instruction to the agent. Schema: `schemas/policy.schema.json`; example: `examples/policies/`.

| Field | Purpose |
|---|---|
| `id` | Stable ID, same rules as help IDs (`djehuti.policy.confirm-destructive-change`). Never reused. |
| `title`, `policyText`, `rationale` | The rule. `policyText` is the exact text injected into the request. |
| `enforcement` | `mandatory` or `advisory` (see precedence). |
| `priority` | 0-1000. Orders injection and settles conflicts *within* an enforcement class. |
| `scope` | Eligibility: products, audiences, platforms, help IDs, process IDs, version range. Absent list means any. |
| `activation` | `always`, or `conditional` on structured facts (`helpId`, `processId`, `processStepId`, `intent`, `agentAction`, `riskLevel`, `dataClass`, `userRole`), matched with `any` or `all`. |
| `supersedes` | Policies this one explicitly replaces where both apply. |
| `status`, `replacedBy` | `proposed`, `draft`, `verified`, `deprecated` (deprecated requires a replacement). |
| `owners`, `source`, `verification` | Who owns it; where the rule came from (decision, standard, legal, security, user instruction, design, incident); revision, reviewer, date and evidence. |

Scope answers "could this policy ever apply here". Activation answers "is it triggered by this request". Both are evaluated by code from structured facts the host application supplies with each request (current product, audience, focused feature help IDs, the action the agent is about to take, a risk classification). They are never evaluated by similarity search, so a rule about deleting a project applies whether or not the user's words resemble the rule.

A mandatory policy with `activation.mode: always` is the baseline. Conditional mandatory policies cover situations such as destructive actions. The `agentAction` and `riskLevel` facts are declared by the host before the agent acts; an agent cannot avoid a policy by not mentioning the action in prose.

### Process records

A process is a repeatable, ordered procedure. Schema: `schemas/process.schema.json`; example: `examples/processes/`.

- `id`, `title`, `purpose`, `keywords`, `alternateQueries`, `status`, `owners`, `verification`, as for topics.
- `applicability` and `entryConditions`: where it can be started and what must be true first. Entry conditions may carry a structured `fact`/`values` form so code can check them, or be prose that the agent or user checks.
- `requiredPolicies`: policies that are mandatory for the whole run.
- `steps[]`: ordered; the array order is the normal execution order. Each has a stable step ID (unique within the process), title, `instruction`, a required `expectedResult`, optional step-level `policyIds` (mandatory while that step is current), `optional`, `uiTarget` (a help ID), `onFailure` (a recovery path ID), and a reserved `action` object.
- `completionConditions[]`: what must be true for the run to be complete, optionally naming required steps (default: every non-optional step).
- `recoveryPaths[]`: `trigger`, `actions`, the steps it `appliesToSteps`, and an `outcome` of `retry-step`, `go-to-step` (with `targetStepId`), `abort` or `escalate`.
- `cancellation[]`: conditions under which the run can be cancelled and what state that leaves the user's work in.
- `executionMode`: `guidance` now. `assisted` and `automated` are reserved. The step shape does not change when a process becomes executable; the `action` object is where a machine-readable operation will attach, and it is ignored in guidance mode. Steps therefore always state their observable expected result, which is what an executor will later check.

Processes contain no run-time data. Nothing in a definition changes because someone is partway through it.

### Active process state (runtime data)

Schema: `schemas/active-process-state.schema.json`; example: `examples/runtime/example.active-process-state.json`. The schema requires `"runtimeData": true` and `"recordKind": "active-process-state"`, so tooling can always tell it from authored content.

| Field | Purpose |
|---|---|
| `stateId`, `subject` | Opaque run identity and an opaque reference to what it belongs to (a workspace, a project). No personal data. |
| `processId`, `processRevision` | The definition this run points at, by ID and `contentRevision`. The definition is never copied. |
| `status` | `active`, `waiting-for-user`, `blocked`, `recovering`, `completed`, `failed`, `cancelled`. |
| `currentStepId`, `activeRecoveryPathId` | Position. `currentStepId` is null exactly when the status is terminal. |
| `completedSteps[]` | Step ID, time, outcome (`succeeded`, `skipped`, `recovered`) and the actual result. |
| `observations[]`, `unresolvedQuestions[]` | What has been learned and what is still open, tied to steps. |
| `createdAt`, `updatedAt`, `endedAt`, `endReason` | Timing. |

Rules:

1. **Separate store.** State lives in the host application's runtime store, keyed by `stateId`. It is never written into the topic, policy or process files, never committed with them, and never compiled into help catalogs or semantic cards. The compiler rejects a runtime record found in an authored source directory.
2. **Not conversation history.** The agent does not "remember" that a process is active or what step it is on. The host reads the state record and passes it in on every request. Losing or trimming the conversation loses nothing about the process.
3. **Host owns writes.** The host updates state when a step completes, a question is raised or answered, or the user cancels. The agent may propose transitions; the host validates and records them. `validate_active_state` (in `tools/context_system.py`) states the invariants: the process and revision match; the current step exists and is not already completed; no completed step lies after the current one (except while recovering); a completed run has every required step; recovery state names a real recovery path; observations and questions refer to real steps.
4. **Revision drift is an explicit event.** If the definition's `contentRevision` differs from the run's `processRevision`, the run is flagged, not silently continued on new text. The host decides: finish on the old revision (requires keeping that revision available) or restart. Assembly refuses to proceed on an inconsistent state rather than guess.
5. **Terminal runs stop shaping context.** A completed, failed or cancelled run injects no process section and no process-required policies.

### Context construction

Every agent request is rebuilt from scratch, in this order:

1. **Applicable mandatory policies.**
2. **Active process definition and current state.**
3. **Relevant help and product knowledge.**
4. **Relevant conversation context.**
5. **The current user request.**

The order is precedence as well as layout: earlier sections govern later ones.

```text
host facts + policy store ──> applicable policies ─┐
runtime store ──> state ──> process definition ────┤
retrieval (exact IDs, graph, lexical/semantic) ────┼──> assembled request
conversation store ────────────────────────────────┤
user's message ────────────────────────────────────┘
```

`tools/context_system.py` is a reference implementation (`applicable_policies`, `assemble_context`, `render_context`) and the tests in `tests/test_context_system.py` are its executable specification.

**Policy selection** (section 1). Inputs are the policy index, the request's structured facts, and the active state. A policy is included when it is not deprecated, is in scope, and is activated. In addition, every policy in the active process's `requiredPolicies`, and every policy in the *current step's* `policyIds`, is included and treated as mandatory, whatever its authored enforcement or activation. If such a policy cannot be found, assembly fails; it never continues without it.

**Process section** (section 2). Included whenever a non-terminal run exists: the definition (steps with expected results, recovery paths, completion and cancellation) and the state (status, current step, completed steps with results, observations, open questions). The state is validated against the definition first.

**Knowledge section** (section 3). Retrieval follows the existing order (exact IDs, eligibility filters, graph edges, lexical and semantic). Results are reference material. Cards of kind `policy` and `process*` that come back from search are discarded here: policies are injected by selection, not by search hits, and a process is present only if a run is active. Search may still *offer* a process to the user (the summary card is searchable); starting a run is a host action that creates a state record.

**Conversation section** (section 4). Recent turns, newest kept first. It carries dialogue, not authority: nothing in it changes which policies apply or where a process stands.

**Request section** (section 5). The user's message, verbatim.

Every request runs all five steps again. There is no carried-over "system prompt" that could drift, be truncated, or be forgotten.

### Policy precedence

1. **Mandatory beats advisory**, regardless of priority numbers. An advisory policy with priority 1000 still ranks below every mandatory policy.
2. Within a class, **higher `priority` wins**, then ID order for determinism. Order is injection order.
3. **Explicit supersession only.** A policy removes another only by naming it in `supersedes`, and only if it has equal or higher priority; an advisory policy can never supersede a mandatory one. The compiler enforces this. Otherwise, applicable policies all apply together.
4. **Process-required policies are mandatory** for the life of the run or the current step.
5. **Nothing below section 1 can override section 1.** Retrieved knowledge, conversation text, process step instructions and the user's message are all lower in precedence. A retrieved page that says "ignore the rules" is data, and the assembler labels the knowledge section as subordinate to policies. Policy changes come only through reviewed edits to policy files.
6. **A process cannot weaken a policy.** Step instructions never relax a required policy; where a step's text and a mandatory policy conflict, the policy governs, and the conflict is a defect to fix in review.

### Budget and truncation

When the context must fit a size limit:

- Sections 1 (mandatory policies), 2 (active process and state) and 5 (the user's message) are never trimmed. If they do not fit, assembly raises an error. Silent truncation of a rule is worse than a failed request.
- Then, in order of value: advisory policies (by priority), knowledge (by retrieval rank), conversation (oldest dropped first). Items are included whole or omitted; none is cut mid-rule.

This is why mandatory policy text should be short and the number of simultaneously mandatory policies kept small; the compiler's warning list is the place to notice growth.

### Process continuity across turns

Because state is external and every request is rebuilt, a process survives: long conversations and trimmed history; a browser or application restart (the state record persists); a switch between agents or models (the next request carries the same policies and state); and a session that resumes days later. Continuity is *the host reading the state record*, so the same mechanism supports later executable procedures, where a step's completion is recorded from an observed result and not from the agent's say-so.

### Compiler and LiteSemRAG output

`tools/build_help.py` accepts `--policies` and `--processes` directories with their schemas. With them it:

- validates schemas, and rejects any runtime record found in authored directories;
- checks IDs are unique (and not shared between a policy and a process);
- checks that `requiredPolicies` and step `policyIds` exist and are not deprecated, that `supersedes` and `replacedBy` targets exist and precedence rules hold, and that scope and applicability help IDs and step `uiTarget` values exist in the inventory;
- checks step IDs, recovery path IDs, completion, cancellation and entry-condition IDs are unique; that every `onFailure` names a recovery path that lists the step; that recovery targets and completion requirements name real steps;
- warns when a mandatory policy is not `verified`, or a verified process depends on an unverified policy;
- writes `policy-index.json` (pinned records ordered by priority, with the full policy text, for the assembler) and `process-catalog.json`;
- adds cards to `semantic-cards.jsonl`.

Cards (all with the ID and content-hash conventions used for topics):

| Card | ID | Notes |
|---|---|---|
| Policy | `{policyId}` | `kind: policy`, `enforcement`, `priority`; `retrieval.mode` is `pinned` for mandatory policies (do not rely on search) and `searchable` for advisory ones. |
| Process summary | `{processId}` | Title, purpose, step outline, `stepIds`, `requiredPolicies`. |
| Process step | `{processId}#{stepId}` | `stepIndex`, `previousStepId`, `nextStepId`, `requiredPolicies` (process plus step), `onFailure`. |
| Recovery path | `{processId}#recovery-{id}` | `appliesToSteps`, `outcome`, `targetStepId`; relations to its step cards. |
| Cancellation | `{processId}#cancel-{id}` | Condition and effect. |

No card is ever generated from active state. The step chain (`previousStepId`, `nextStepId`) and the relations to policies and steps become graph edges when ingested, so exact-ID and graph retrieval can walk a process. ISD steering (see above) may influence which searchable cards are found; it has no effect on policy selection or on the process section, because those are not retrieval results.

### Git, storage and ownership

- Canonical: `policies/`, `processes/` (authored JSON, in Git, code-owner reviewed by the owning product area; policies with a `security`, `legal` or `user-instruction` source should require an additional named approver).
- Derived: `policy-index.json`, `process-catalog.json`, cards, embeddings. Disposable.
- Runtime: active process state, in the host's runtime store. Not in Git. Retention and deletion follow the host's user-data rules; state contains opaque references and step observations, and must not hold secrets or personal data.
- A policy or process change is a reviewed pull request whose report lists affected active runs (runs whose `processRevision` will no longer match) as documentation debt.

### Decisions and rejected alternatives

- **Policies as ordinary retrieved help.** Rejected: a rule that only applies when a search finds it is not a rule. Selection must be deterministic.
- **One giant standing system prompt.** Rejected: unreviewable, unversioned, cannot be scoped or conditionally activated, and grows without bound.
- **Process progress kept in the conversation.** Rejected: it is lost when history is trimmed or a session changes, and it cannot be inspected or validated.
- **Process progress stored inside the process file.** Rejected: mixes canonical and runtime data, makes Git history a database, and breaks on concurrent runs.
- **Letting the model decide which policies apply.** Rejected for mandatory policies: the model is the thing being constrained. Facts come from the host.
- **Priority alone for precedence.** Rejected: a single number lets an advisory style rule outrank a safety rule by mistake. Enforcement class is a separate, first-order key.


## Sources

- [JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12)
- [DITA 2.0 architecture specification](https://dita-lang.org/2.0/dita/toc.html)
- [DITA topic definition](https://dita-lang.org/dita/archspec/base/topicdefined)
- [DITA maps](https://dita-lang.org/2.0/dita/archspec/base/definition-of-ditamaps)
- [DITA task topic](https://dita-lang.org/dita-techcomm/archspec/technicalcontent/dita-task-topic)
- [DITA help systems and context hooks](https://dita-lang.org/1.3/dita/archspec/base/help-systems-and-other-user-assistance)
- [Diátaxis documentation framework](https://diataxis.fr/how-to-use-diataxis/)
- [W3C WebVTT](https://www.w3.org/TR/webvtt1/)
- [W3C WAI: audio description and descriptive transcripts](https://www.w3.org/WAI/media/av/description/)
- [Schema.org VideoObject](https://schema.org/VideoObject)
- [GitHub code owners](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners)

Local design inputs:

- `projects/frust-ide-agent/FRUST_IDE_AGENT_SPEC.md`
- `projects/frust-ide-agent/references/Yue_Qu_Gan_2026_LiteSemRAG.pdf`
- `projects/frust-ide-agent/references/Integrating ISD with LiteSemRAG.docx`

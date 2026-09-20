# DjehutiSuite Help System Architecture

Status: research prototype

Date: 2026-09-20

Scope: technology-independent source model, synchronization, browser publication, video, and LiteSemRAG + ISD ingestion

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

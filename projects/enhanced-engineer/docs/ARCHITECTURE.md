# Enhanced Engineer Architecture

## Purpose

The Enhanced Engineer is the common intelligence layer for DjehutiSuite applications. It combines four previously separate concerns:

1. authored help and application knowledge;
2. policies the Engineer must continually obey;
3. active multi-turn procedures the Engineer must not forget;
4. typed or spoken conversation through the same agent path.

Voice is not a second agent. It is another way to enter and hear ordinary Engineer turns.

## Recording-session experience

The intended suite interaction is:

```text
normal recording route
        |
        | user presses mapped Engineer control
        v
Engineer Voice Mode acquires communication mic/headset route
        |
        +-> listen -> transcribe -> context -> LLM -> speak
        |                         ^                   |
        +-------------------------+-------------------+
        |
        | user presses mapped Engineer control again
        v
restore the exact normal recording route
```

The Engineer does not know or care whether the mapped control originated from an IDE key, a suite button, or another control surface. It receives only a toggle event.

The IDE experiment binds F9. First press turns voice mode on; second press turns it off. Holding the key is never required.

## Boundary with the studio

`AudioRouteManager.acquire_engineer_route()` is the only suite audio-routing boundary. Its implementation must:

- snapshot the relevant preexisting route;
- move the designated communication input/output to the Engineer;
- leave unrelated recording tracks and transport state alone;
- return an idempotent lease whose `restore()` method reinstates the snapshot;
- restore on normal exit, cancellation, provider failure, or application shutdown.

The Enhanced Engineer does not stop recording transport, manipulate track routing directly, select devices, or understand the studio graph. Station will eventually provide the adapter.

For the IDE prototype, the adapter opens the Windows/JUCE default input and output. The lease can simply own and close that temporary audio-device session because there is no recording graph to restore.

## Toggle voice-mode lifecycle

```text
OFF
 -> STARTING
 -> LISTENING
 -> TRANSCRIBING
 -> THINKING
 -> SPEAKING
 -> LISTENING       (another utterance in the same session)

any active state
 -> STOPPING
 -> OFF             (second F9 press)

any failed state
 -> ERROR            (route already restored)
 -> OFF              (reset)
```

Speech input owns utterance endpointing while voice mode is active. After it emits a final utterance, microphone recognition stops while the Engineer thinks and speaks. It resumes only after speech output completes. This prevents the Engineer from transcribing its own voice.

The second toggle has priority over all work. It invalidates old callbacks, cancels STT, agent work, and TTS, then restores the route exactly once.

## Speech and agent interfaces

The reference implementation defines four replaceable interfaces:

- `AudioRouteManager` acquires the temporary Engineer communication route.
- `SpeechInput` performs capture, endpointing, and STT and emits final utterances.
- `EngineerAgent` sends transcribed text through context assembly and the configured BYOK LLM.
- `SpeechOutput` turns response text into audio and reports completion.

Local speech, cloud speech, and future suite-native speech engines fit behind these contracts. The LLM provider remains independent of both speech providers.

## Context order

Every typed or spoken request is assembled in this order:

1. mandatory applicable policies;
2. active process definition and current runtime state;
3. advisory applicable policies;
4. retrieved help and product knowledge;
5. relevant conversation;
6. current request.

Mandatory policies and active process state are selected deterministically. They never compete with help cards for search ranking. Missing active-process definitions or revisions fail loudly instead of silently dropping the process.

The provider-specific prompt renderer consumes ordered structured sections. This keeps the context decision auditable and avoids burying obligations inside an undifferentiated prompt.

## LiteSemRAG implementation

The SQLite implementation is a disposable read model built from the existing help compiler's semantic cards. It does not use an LLM during indexing.

### Graph layers

- **Document:** canonical topic, policy, or process identity.
- **Chunk:** stable authored block, process step, recovery path, or policy body.
- **Semantic:** the authored meaning represented by that card.
- **Token:** exact lexical anchors supplied by authors or extracted deterministically.
- **Entity:** product objects and concepts mentioned by cards.
- **Context:** stable application help IDs.

Edges include:

- `CONTAINS`: document to chunk;
- `EXPRESSES`: chunk to semantic meaning;
- `LEXICALIZES`: token to meaning with field-dependent weight;
- `RELATED_TO`: explicit authored relationship;
- `REQUIRES_POLICY`: process or step dependency;
- `MENTIONS`: semantic meaning to entity;
- `RESOLVES_TO`: application context ID to meaning;
- `CO_OCCURS`: meanings sharing at least two useful lexical anchors.

### Retrieval

A query produces a small query-specific score graph:

1. tokenize the request deterministically;
2. find full-text candidates through SQLite FTS5;
3. score exact authored, title, and body token occurrences;
4. expand through explicit relationships, policy dependencies, and semantic co-occurrence;
5. prefer verified cards over draft and proposed cards;
6. return card IDs, scores, sources, and human-readable reasons.

This is substantially different from doing SQL `LIKE` over entire documents. Retrieval operates on stable authored meanings, keeps exact identifiers strong, and can explain why each card entered the context.

The implementation intentionally has no provider embedding dependency. A local embedding adapter can later add an additional similarity signal without changing source cards or graph identity. Authored semantics, exact tokens, and explicit relationships remain available when embeddings are absent.

## Canonical and derived data

Canonical Git data remains in the DjehutiSuite help/context project:

```text
topic JSON
policy JSON
process JSON
schemas
```

Runtime state and derived data are separate:

```text
active process state       runtime authority
semantic-cards.jsonl       compiled read model
enhanced-engineer.db       disposable retrieval index
voice session state        transient runtime state
```

The Enhanced Engineer never writes answers, embeddings, or runtime process state back into canonical help files.

## Tool execution path

The reference implementation now includes governed project inspection, file creation and editing, and controlled build, test, compiler, and diagnostic process tools. Product-specific tools follow the same turn:

```text
voice/text request
 -> mandatory policies
 -> active procedure
 -> product knowledge
 -> target resolution
 -> typed tool call
 -> observed result
 -> process-state update
 -> visual/spoken response
```

STT confidence does not establish target confidence. A perfect transcript of "turn down the guitar" may still identify multiple tracks. Target resolution and confirmation belong to the Engineer/tool layer, not the speech layer.

## Access and approval boundary

Tool authorization is deterministic host code and is evaluated after the model proposes a typed call but before any adapter runs. The model cannot change its access level, approve its own call, widen a project root, or mark a capability safe.

Standing access levels are cumulative:

- **Observe:** project reads, search, help, LiteSemRAG evidence, and diagnostics.
- **Workspace:** Observe plus file and directory creation or modification inside explicit workspace roots.
- **Engineer:** Workspace plus builds, tests, compiler checks, controlled execution, and debugger sessions.
- **System:** broader filesystem, process, network, application, installation, and device adapters.

The host also sets a maximum level. Calls above that ceiling are denied. Calls above the standing level but not above the ceiling may request approval when the session policy permits it. Destructive and privileged calls require approval even when their nominal level is already granted.

An approval names one canonical tool-call fingerprint containing the tool, complete arguments, and resolved target. It expires quickly and is consumed once. Changing an argument or target requires a new approval. Forbidden tool declarations remain denied even when presented with an otherwise valid approval.

## Typed tool registry

The tool registry is the only execution path. Each registration combines a provider-facing JSON argument schema, human purpose and usage guidance, deterministic access declaration, target resolver, and host implementation. Calls are rejected before the implementation runs when the tool is unknown, the path is out of scope, approval is missing, or the session ceiling is too low.

Workspace creation and edits are deliberately separated. New-file creation refuses to overwrite. Focused editing requires exact old text and rejects ambiguous matches. Complete replacement is a separate tool that always requires an exact approval. Process execution accepts an argument array rather than shell text, limits working directories to the granted workspace, enforces a host executable allow-list and timeout, and captures exit code, standard output, and standard error.

LiteSemRAG tool cards are generated from these same registrations. Tool name, purpose, usage guidance, argument schema, access level, scope, risk, and approval behavior therefore have one source of truth. The cards teach selection and procedure; provider schemas constrain the actual call; the access controller remains authoritative over execution.

## IDE adapter

The first operational adapter should remain deliberately small:

- F9 key-down toggles voice mode; keyboard auto-repeat is ignored.
- JUCE opens the default system input and output devices.
- The default gaming headset supplies microphone and playback on the current machine.
- No audio-device chooser is required initially.
- A local or cloud `SpeechInput` implementation supplies endpointed utterances.
- The existing BYOK text provider receives the transcribed request.
- A `SpeechOutput` implementation speaks the response.
- Typed and spoken turns share the same transcript and context diagnostics.

This validates the complete mental/context loop before a FrustIDE adapter or Station audio and application tools are connected.

## What is not implemented here

- MIDI or control-surface protocols;
- Station's audio graph or route adapter;
- real microphone capture or TTS engines;
- product-specific FrustIDE and Station tool adapters;
- an LLVM source-level breakpoint and debugger service;
- automatic track selection;
- unrestricted shell or filesystem access;
- ISD trajectory metrics.

ISD remains the next retrieval-control layer. Reference drift, torsional resistance, and turning curvature can adjust future query weighting and recovery, but they must not mutate canonical knowledge or override mandatory policies.

## Acceptance criteria satisfied by the reference implementation

- Semantic cards build all four core LiteSemRAG layers.
- Retrieval uses exact anchors, full text, graph relations, and co-occurrence.
- Every retrieval hit reports its evidence.
- Mandatory policies precede retrieved help.
- Active process state survives independently of conversation text.
- A missing active process fails context assembly.
- One F9-on session supports multiple utterances.
- Speech input pauses while the Engineer speaks.
- Second F9 press cancels work and restores the route.
- Provider failure restores the route.
- No device-protocol or Station dependency exists.

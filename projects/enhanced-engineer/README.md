# Enhanced Engineer

Enhanced Engineer combines the DjehutiSuite help/context system, a deterministic LiteSemRAG read model, persistent policies and processes, and a toggle voice-mode control surface.

It is a research implementation. It deliberately contains no Station code, no MIDI code, no device-specific switch code, and no unrestricted agent tools.

## Implemented

- SQLite graph layers for documents, chunks, authored semantic meanings, lexical tokens, entities, and application contexts.
- Deterministic ingestion of the help compiler's `semantic-cards.jsonl`.
- Full-text, exact-token, co-occurrence, and explicit-relation retrieval with evidence.
- Context assembly that places mandatory policies and active process state ahead of retrieved knowledge.
- Deterministic tool access levels and exact, expiring, single-use approvals.
- A press-on/press-off Engineer Voice Mode controller.
- Abstract audio-route, speech-input, agent, and speech-output contracts.
- Route restoration after normal exit, cancellation, or failure.
- Tests with fake audio and agent adapters; no hardware is required.

## Tool access

The Engineer framework separates standing access from per-call approval:

- `OBSERVE` reads project material and diagnostics.
- `WORKSPACE` creates and edits content inside an explicitly granted project root.
- `ENGINEER` adds builds, tests, compiler checks, and debugger sessions.
- `SYSTEM` covers broader filesystem, process, network, device, and application control.

Every tool declares its minimum level, risk class, and path scope. The runtime makes the authorization decision before invoking the tool; the LLM cannot grant itself access. Destructive and privileged calls always require an exact approval. Approvals are argument-bound, path-bound, expiring, and single-use. A forbidden capability cannot be approved.

## Run

```powershell
cd "D:\000 Tech Research\projects\enhanced-engineer"
py -m unittest discover -s tests -v
```

Build a LiteSemRAG database from the DjehutiSuite help compiler output:

```powershell
py tools\build_index.py `
  "D:\000 Tech Research\projects\djehuti-suite-help\build\semantic-cards.jsonl"
```

The generated database is `build\enhanced-engineer.db` and is intentionally ignored by Git.

## IDE experiment

- Bind `F9` key-down to `EngineerVoiceController.toggle()`; ignore keyboard auto-repeat.
- First press opens Engineer Voice Mode using the Windows/JUCE default input and output devices.
- Speech endpointing produces one or more Engineer turns while the mode remains active.
- Second press cancels pending voice work and exits the mode.

For the current machine, the default devices are expected to be the gaming headset microphone and headphones. No device selector is required for the first experiment.

See `docs/ARCHITECTURE.md` for the suite-level audio-route contract and integration boundaries.

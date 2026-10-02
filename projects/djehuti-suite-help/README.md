# DjehutiSuite Help System Research Prototype

This project is a technology-independent prototype for a DjehutiSuite help system that serves two consumers from one reviewed source:

- a conventional, searchable help browser with context-sensitive links and video;
- the LiteSemRAG + ISD knowledge layer used by the Virtual Engineer.

It also defines the rest of the agent context: **policies** (rules the Virtual Engineer must follow), **processes** (repeatable multi-step procedures) and **active process state** (runtime data about a run in progress), assembled with help into every agent request. See the "Agent context system" section of `docs/ARCHITECTURE.md`.

The canonical source is versioned JSON. Browser catalogs, context maps, search records, and semantic cards are generated artifacts. Application code contributes a generated help inventory containing stable help IDs and behavior signatures; CI compares that inventory with the authored topics so user-visible behavior cannot change silently.

## Prototype layout

- `docs/ARCHITECTURE.md` - research, decisions, synchronization model, and rollout plan.
- `schemas/help-topic.schema.json` - authored topic contract (JSON Schema Draft 2020-12).
- `schemas/help-inventory.schema.json` - application-generated public feature contract.
- `examples/topics/` - representative authored topics. The `example.*` namespace is intentionally non-production.
- `examples/help-inventory.json` - representative application inventory.
- `tools/build_help.py` - validates sources, checks synchronization, and compiles consumer artifacts.
- `schemas/policy.schema.json`, `schemas/process.schema.json` - authored policy and process contracts.
- `schemas/active-process-state.schema.json` - contract for the **runtime** process-state record (never authored, never in Git).
- `examples/policies/`, `examples/processes/` - example policies (mandatory always, mandatory conditional, advisory) and a multi-step process with recovery and cancellation. `examples/runtime/` holds an example state document, clearly marked as runtime data.
- `tools/context_system.py` - policy/process validation, card generation, active-state validation, and a reference context assembler.
- `tests/test_build_help.py` - regression tests for compilation and drift detection.
- `tests/test_context_system.py` - tests for policy/process compilation, stable IDs and step relationships, state validation, policy precedence, and context assembly.

## Run the prototype

From this directory:

```powershell
python -m pip install -r requirements.txt

python tools/build_help.py `
  --topics examples/topics `
  --inventory examples/help-inventory.json `
  --output build

python -m unittest discover -s tests -v
```

The compiler writes:

- `build/help-catalog.json` for a help browser;
- `build/context-map.json` for context-sensitive help;
- `build/semantic-cards.jsonl` for LiteSemRAG ingestion;
- `build/sync-report.json` for CI and documentation ownership;
- `build/policy-index.json` (pinned policy records for the context assembler) and `build/process-catalog.json`;
- policy and process cards in `semantic-cards.jsonl`, next to the help cards.

`jsonschema` is the only non-standard Python dependency. The compiler exits nonzero for schema errors, uncovered required help IDs, stale behavior signatures, broken topic relations, duplicate IDs, or invalid block anchors.

## Policies, processes and runtime state

Policies and processes are authored JSON in Git and compile like topics (`--policies`, `--processes`; the defaults are the examples). Mandatory policies are never found by search: the context assembler selects them from structured request facts and the active process and injects them into every applicable request. Active process state is runtime data held by the host application; the compiler rejects it if it appears in an authored directory and never turns it into cards.

## Governing rule

Git-tracked topic, policy and process JSON and application-generated inventory JSON are authoritative; active process state is runtime data and belongs to the host's runtime store. Databases, embeddings, rendered HTML, and indexes are disposable read models and must never become the editing source.

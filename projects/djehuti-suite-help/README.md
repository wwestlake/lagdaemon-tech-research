# DjehutiSuite Help System Research Prototype

This project is a technology-independent prototype for a DjehutiSuite help system that serves two consumers from one reviewed source:

- a conventional, searchable help browser with context-sensitive links and video;
- the LiteSemRAG + ISD knowledge layer used by the Virtual Engineer.

The canonical source is versioned JSON. Browser catalogs, context maps, search records, and semantic cards are generated artifacts. Application code contributes a generated help inventory containing stable help IDs and behavior signatures; CI compares that inventory with the authored topics so user-visible behavior cannot change silently.

## Prototype layout

- `docs/ARCHITECTURE.md` - research, decisions, synchronization model, and rollout plan.
- `schemas/help-topic.schema.json` - authored topic contract (JSON Schema Draft 2020-12).
- `schemas/help-inventory.schema.json` - application-generated public feature contract.
- `examples/topics/` - representative authored topics. The `example.*` namespace is intentionally non-production.
- `examples/help-inventory.json` - representative application inventory.
- `tools/build_help.py` - validates sources, checks synchronization, and compiles consumer artifacts.
- `tests/test_build_help.py` - regression tests for compilation and drift detection.

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
- `build/sync-report.json` for CI and documentation ownership.

`jsonschema` is the only non-standard Python dependency. The compiler exits nonzero for schema errors, uncovered required help IDs, stale behavior signatures, broken topic relations, duplicate IDs, or invalid block anchors.

## Governing rule

Git-tracked topic JSON and application-generated inventory JSON are authoritative. Databases, embeddings, rendered HTML, and indexes are disposable read models and must never become the editing source.

# Djehuti Electronics Lab Knowledge

This folder is the filesystem-backed Information Space Design area for the
embedded BYOK assistant.

The FrustIDE agent uses LiteSemRAG cards and a generated SQLite read model.
This prototype starts with the same card-first idea, but keeps the electronics
app on normal files:

```text
prototype/knowledge/cards/*.jsonl
prototype/projects/current/.djehuti/MEMORY_PROJECT_CARDS.jsonl
```

Each JSONL line is one durable card. Cards are intentionally small so the agent
retrieves a few relevant policies/tools/domain facts per request instead of
stuffing the whole project spec into every model call.

## Card Families

- `electronics_process_cards.jsonl`: workflow, gates, and evidence rules.
- `electronics_tool_cards.jsonl`: tool descriptions that should match live host
  tools exposed to the assistant.
- `electronics_domain_cards.jsonl`: stable domain principles and design model
  facts.
- `electronics_cookbook_seed.jsonl`: structured engineering cookbook entries
  with topology selection guidance, equations, tool recipes, analysis recipes,
  validation criteria, failure modes, iteration rules, and capability gaps.
- `COOKBOOK_ACCEPTANCE_GOALS.json`: representative unseen engineering goals
  used to validate agent behavior across passive filters, active filters,
  transistor amplifiers, op-amp circuits, rectifier supplies, regulators,
  oscillators, control loops, and sensor interfaces.
- project memory cards: project-local notes approved or authored during work.

The runtime currently reads cards directly from JSONL. A disposable SQLite
LiteSemRAG read model can be added later without changing the card source of
truth.

See `COOKBOOK_SCHEMA.md` for the structured cookbook entry model. The BYOK
agent can explicitly search this knowledge through the `cookbook_lookup` tool.
See `COOKBOOK_TAXONOMY.json` for the target coverage map. The BYOK agent can
inspect current coverage and missing categories through `cookbook_coverage`.
After cookbook edits, use `cookbook_validate` to check required fields,
taxonomy alignment, duplicate IDs, and malformed card entries.
The build-time validator also checks acceptance-goal coverage and verifies that
each goal references real cookbook cards.
The BYOK agent can inspect the representative acceptance suite through
`cookbook_acceptance_goals`, optionally filtered by domain or exact goal id.
Before executing an acceptance goal, use `cookbook_acceptance_start` to create a
JSON and Markdown evidence scaffold under the generated run artifacts folder.
During the run, use `cookbook_acceptance_record` to append retrieved cards,
tool calls, artifacts, criteria results, notes, and capability gaps to that
report so validation does not depend on chat transcript memory.
When a gap should become reusable engineering work, use `capability_gap_record`
to append it to the project-local `.djehuti/CAPABILITY_GAPS.jsonl` registry.

For build-time validation, run:

```powershell
cmake --build prototype/build --target validate_cookbook --config Debug
ctest --test-dir prototype/build -C Debug -R CookbookValidation --output-on-failure
```

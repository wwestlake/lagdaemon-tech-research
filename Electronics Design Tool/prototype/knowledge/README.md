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
- project memory cards: project-local notes approved or authored during work.

The runtime currently reads cards directly from JSONL. A disposable SQLite
LiteSemRAG read model can be added later without changing the card source of
truth.

See `COOKBOOK_SCHEMA.md` for the structured cookbook entry model. The BYOK
agent can explicitly search this knowledge through the `cookbook_lookup` tool.

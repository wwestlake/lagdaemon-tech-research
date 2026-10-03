# Electronics Cookbook Schema

The cookbook is a filesystem-backed, card-first engineering knowledge source for
the BYOK electronics agent. It is not a prose manual. Each cookbook card should
be a structured JSONL object stored under `prototype/knowledge/cards`.

The runtime currently indexes the common LiteSemRAG card fields:

- `id`
- `kind`
- `title`
- `source`
- `tokens`
- `priority`
- `status`
- `text`

Cookbook entries may also carry richer structured fields. The current runtime
preserves those fields in the source file and retrieves the card summary through
`cookbook_lookup`; future read models can index them directly without changing
the source of truth.

## Recommended Fields

- `id`: Stable entry identifier, such as `electronics.cookbook.filter.rlc_high_pass_matched`.
- `kind`: Use `cookbook_recipe`, `cookbook_analysis`, `cookbook_topology`, or
  `cookbook_measurement`.
- `category` and `subcategory`: Searchable hierarchy.
- `purpose`: What engineering problem this entry solves.
- `whenToUse` and `whenNotToUse`: Selection boundaries.
- `requiredComponents` and `optionalComponents`: Component archetypes, not
  marketplace parts.
- `topology`: Human and machine-readable construction description.
- `parameters`: Requirement names and meanings.
- `designEquations`: Equations needed for initial values.
- `designProcedure`: Ordered calculation and construction steps.
- `toolRecipe`: Application tools the agent should use.
- `analysisRecipe`: SPICE/instrument analysis steps.
- `validationCriteria`: Measured or simulated evidence needed to pass.
- `failureModes`: Common reasons the design misses the requirements.
- `iterationRules`: What to change after a failed validation.
- `capabilityGaps`: Missing app/tool capabilities that must not be faked.
- `relatedEntries`: IDs of useful neighboring entries.
- `provenance`: Author, source family, revision, and verification state.

## Execution Rule

A cookbook recipe may propose an analysis, but the agent must still use real app
tools, solver output, and instrument data to validate a design. If a required
tool is missing, the agent must report a capability gap and the coding agent
should implement the missing reusable capability instead of fabricating evidence.

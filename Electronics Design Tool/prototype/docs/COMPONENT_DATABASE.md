# Component Database Research Notes

The electronics tool should own a local component database instead of treating
schematic symbols as the whole component model.

## Layered Component Identity

Use four layers:

- **Archetype**: generic design object, such as `discrete.bjt.npn`,
  `analog.op_amp`, `source.ac_voltage`, or `passive.resistor`.
- **Family**: common part identity without a specific manufacturer, such as
  `2N2222`, `LM741`, `1N4148`, or `NE555`.
- **Manufacturer part**: vendor-specific part with datasheet provenance, package
  options, limits, claims, and model references.
- **Placed instance**: schematic item such as `Q3`, `R8`, or `U1`, with local
  parameter overrides and a selected family or manufacturer binding.

This lets the user sketch quickly with generic objects and bind exact parts
later.

## First Storage Shape

For research, JSON files are acceptable. SQLite is a good next step when query
volume, indexing, or concurrent updates matter.

Suggested collections:

- `archetypes`
- `part_families`
- `manufacturer_parts`
- `symbols`
- `packages`
- `simulation_models`
- `datasheets`
- `claims`
- `ingestion_runs`

## Datasheet Claims

Extracted datasheet facts are claims, not unquestioned truth. Store:

- normalized value and unit
- source URL or cached document ID
- page/table/section when available
- raw evidence text when practical
- extraction confidence
- verification status

## Seed Library

Seed common components locally so normal design work starts immediately:

- resistors, capacitors, inductors
- 2N2222 / 2N3904 class NPN transistors
- 2N3906 class PNP transistors
- 1N4148, 1N400x diodes
- LM741, LM358, TL072 op amps
- NE555 timer
- common logic gates
- batteries, DC supplies, AC/signal sources
- power and ground bus objects

The agent should search this local database before attempting provider,
manufacturer, or web ingestion.

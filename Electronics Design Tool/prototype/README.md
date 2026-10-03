# Djehuti Electronics Lab Prototype

This is a standalone research shell for the electronics design tool.

This app is licensed under GPL-3.0 as part of the SPICE/Xyce-oriented
electronics tool track. See `../LICENSE`.

It is intentionally outside the FrustLang repository:

```text
D:\000 Tech Research\Electronics Design Tool\prototype
```

The shell reuses the research dock manager from FrustLang, but this project owns its own app target and electronics-specific panels.

## Panels

- Component Library
- Schematic
- Simulation
- Instruments
- Frust Math Console
- BYOK Agent
- Properties
- Spec Ingestion

## Build

Configure with CMake, pointing at JUCE if needed:

```powershell
cmake -S "D:\000 Tech Research\Electronics Design Tool\prototype" -B "D:\000 Tech Research\Electronics Design Tool\prototype\build" -DJUCE_PATH=D:\JUCE
cmake --build "D:\000 Tech Research\Electronics Design Tool\prototype\build" --config Debug
```

The app output is:

```text
D:\000 Tech Research\Electronics Design Tool\prototype\bin\Debug\djehuti_electronics_lab.exe
```

## Project State

`File > Save Project` writes the editable circuit model to:

```text
D:\000 Tech Research\Electronics Design Tool\prototype\projects\current\circuit.json
```

`File > Open Project...` loads that same circuit JSON back into the schematic.
Simulation commands still export run artifacts under `sim/xyce/runs/generated`.
`Circuit > Run ERC` writes `erc_report.md` in that generated run folder.
It also writes `erc_tool_result.json` for the BYOK agent tool surface.
`Agent > Export Tool Manifest` writes `assistant_tools.json` in the same folder.
The BYOK Agent panel now uses the FrustIDE `ai_provider` library for OpenAI
profiles, API keys, model selection, and tool-calling. Keys are stored outside
the repo in user app data.

The app also starts a loopback local agent API and writes its discovery file to:

```text
%APPDATA%\DjehutiElectronicsLab\agent-api.json
```

The API exposes message/session/cancel endpoints so external tools can talk to
the embedded electronics agent with the same UI session and tool surface.
The first live schematic tools are `circuit_inspect`, `schematic_place_symbol`,
`schematic_connect`, `instrument_open_panel`, `circuit_run_erc`, and
`simulation_export_artifacts`.
The cookbook validation tools add `cookbook_lookup`, `cookbook_coverage`,
`cookbook_validate`, `cookbook_acceptance_goals`,
`cookbook_acceptance_start`, and `cookbook_acceptance_record` so acceptance
runs can create durable reports and record tool-derived evidence.

Assistant knowledge uses filesystem cards instead of Suite VFS storage:

```text
D:\000 Tech Research\Electronics Design Tool\prototype\knowledge\cards
D:\000 Tech Research\Electronics Design Tool\prototype\projects\current\.djehuti\MEMORY_PROJECT_CARDS.jsonl
```

Instrument placement is moving toward schematic nodes instead of a fixed bench.
The component library includes a two-channel oscilloscope and a digital
multimeter. Place them on the diagram, wire their pins like components, and
double-click an instrument node, or use `instrument_open_panel`, to open a
floating instrument window.

The schematic canvas defaults to snap-on editing so pins, wire endpoints, rail
taps, and junctions land on the same grid. The toolbar also exposes Snap,
zoom-out, zoom-reset, and zoom-in controls. Zoom is view-only: saved circuit
coordinates and agent tool coordinates stay in schematic space.

Diagram rendering should keep converging on common schematic practice: use
recognized IEEE/ANSI or IEC-style symbols where practical, keep wires and pins
aligned to a regular grid, make junctions explicit, avoid ambiguous near-miss
connections, and prefer readable left-to-right signal flow over dense wiring.

## Frust And LLVM

This prototype may embed Frust for the math console, compiled circuit previews,
plugins, and generated simulation kernels.

Frust uses LLVM, but this app must not build LLVM. LLVM is treated as an existing
configured dependency supplied by the Frust toolchain/package. When Frust is
linked into this app, use the same configured LLVM package that Frust already
uses. Do not add LLVM source builds, LLVM subdirectories, or broad compiler
rebuild steps to this prototype.

Expected integration shape:

```text
electronics circuit JSON
-> electronics IR
-> Frust source or in-memory Frust module
-> existing Frust compiler/runtime
-> existing configured LLVM package
```

The electronics app target should remain small and scoped to the UI, circuit
model, agent workflow, solver adapters, and calls into the existing Frust
toolchain.

## Notes

This first shell is not the electronics engine. It is the platform workspace where we can start adding:

- circuit JSON model
- component ingestion
- BYOK agent integration
- LiteSemRAG cards
- Frust math console binding
- SPICE/offline simulation
- compiled Frust realtime preview

## Component Database Direction

Component records should not collapse generic symbols, common part families, and
manufacturer-specific datasheets into one thing. The research model separates:

- generic archetypes
- common part families
- manufacturer parts
- placed schematic instances

See `docs/COMPONENT_DATABASE.md` for the current storage direction.
The first seed data file is `data/component_seed.json`.

Parts sourcing is tracked separately from verified specifications. See
`docs/PARTS_SOURCING.md` for the current sourcing direction.

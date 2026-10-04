# Djehuti Electronics Lab Prototype

This is a standalone research shell for the electronics design tool.

This app is licensed under GPL-3.0 as part of the SPICE/Xyce-oriented
electronics tool track. See `../LICENSE`.

It is intentionally outside the FrustLang repository:

```text
D:\000 Tech Research\Electronics Design Tool\prototype
```

The shell reuses the research dock manager from FrustLang, but this project owns its own app target and electronics-specific panels.

## Agent Parity Rule

Anything a user can do in the app, the BYOK agent can do through a tool:
create, change, edit, delete. A UI feature is not finished until its agent
tool, tool description, and tool card
(`knowledge/cards/electronics_tool_cards.jsonl`) exist. See
`docs/HANDOFF_SCHEMATIC_HIERARCHY.md` for the current gaps and plan.

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

Work is organised as named projects that contain named diagrams:

```text
<location>\<Project Name>\          default location: Documents\Djehuti Electronics Lab\Projects
  project.json                     name, created, diagram list, last open diagram
  diagrams\<Diagram>.diagram.json  one diagram: all sheets, sub-diagrams, groups, wiring
  outputs\<Diagram>\               netlists, ERC reports, simulations, images for that diagram
  memory\                          the agent's project memory and capability gaps
  deleted\                         diagrams removed from the project (moved, never erased)
```

`File > New Project...` asks for a name and a storage location, then for the
first diagram's name and saves it blank. The File menu also has Open Project,
Recent Projects, Rename Project, New Diagram, Diagrams (switcher), Rename /
Duplicate / Delete Diagram, Save Diagram and Save Diagram As. The window title
shows `Project > Diagram`; the last project and diagram reopen on start;
switching diagrams saves the current one; closing asks about unsaved changes.
The agent has matching `project_*` and `diagram_*` tools.

The BYOK Agent panel now uses the FrustIDE `ai_provider` library for OpenAI
profiles, API keys, model selection, and tool-calling. Keys are stored outside
the repo in user app data.

The app also starts a loopback local agent API and writes its discovery file to:

```text
%APPDATA%\DjehutiElectronicsLab\agent-api.json
```

The API exposes message/session/cancel endpoints, plus `POST /v1/tools/call` to run any agent tool directly without the model, so external tools can talk to
the embedded electronics agent with the same UI session and tool surface.
The first live schematic tools are `circuit_inspect`, `schematic_place_symbol`,
`schematic_connect`, `instrument_open_panel`, `circuit_run_erc`, and
`simulation_export_artifacts`.
The cookbook validation tools add `cookbook_lookup`, `cookbook_coverage`,
`cookbook_validate`, `cookbook_acceptance_goals`,
`cookbook_acceptance_start`, `cookbook_acceptance_record`, and
`cookbook_acceptance_summary`, plus `capability_gap_record`, so acceptance runs
can create durable reports, summarize prior evidence, and preserve reusable
missing-capability gaps in project memory.

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

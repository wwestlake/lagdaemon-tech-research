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

Instrument placement is moving toward schematic nodes instead of a fixed bench.
The component library includes a two-channel oscilloscope and a digital
multimeter. Place them on the diagram, wire their pins like components, and
double-click an instrument node to open a floating instrument window.

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

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

## PCB (board outline)

The **PCB** tab holds the diagram's board, saved in the diagram file under
`pcb`. The outline comes from a standard board (Eurocard sizes, Raspberry Pi
HAT, Arduino Uno envelope, credit card, 100 x 100 / 50 x 50 mm, with their
mounting holes), a parametric shape (rectangle, rounded or chamfered corners,
L, U, T, circle, regular polygon such as a hexagon), or one drawn by hand:
any straight-edged outline. Corners drag, a click on an edge adds a corner,
right-click deletes; mounting holes and cutouts are added and dragged; layers,
thickness and copper-to-edge clearance are set beside it; problems (crossing
edges, holes off the board) show as the outline changes. Ctrl+Z undoes. The
board geometry, outline generators and validation come from the DjehutiRoute
library (`D:\DjehutiRoute`, MIT), which also provides the router. The agent
has `pcb_board_get`, `pcb_board_list_options`, `pcb_board_use_standard`,
`pcb_board_set_shape`, `pcb_board_set_outline`, `pcb_board_add_hole`,
`pcb_board_add_cutout`, `pcb_board_remove` and `pcb_board_set_stackup`.

**Parts and routing.** *Update parts from schematic* puts the diagram's
parts on the board as footprints carrying the schematic's nets (the same net
names Analytics shows). Every symbol has a default footprint and alternatives
(0805/1206/axial passives, SOD-123, SOT-23, TO-92, TO-220, SOIC-8/DIP-8 for the
741, trimmers, headers for sources and batteries, relays, transformers, test
points); instruments and ideal controlled sources stay off the board. The
first sync lays the whole board out automatically (most-connected first, each
part where its pads land nearest the pads already placed on the same nets,
inside any outline, clear of holes, cutouts, the edge and other parts); later
syncs keep parts where they are and place only new ones. Parts drag on the
canvas, rotate with R or right-click, and change footprint in the sidebar.
Yellow lines are the connections still to make. *Route board* runs
DjehutiRoute on a worker thread with the track width, clearance and via rules
from the sidebar, draws the copper per layer with vias, and runs the exact
design-rule check; any violation is marked on the board. Moving a part,
changing a rule or the board clears the stale copper. Parts, rules and the
routed copper are saved in the diagram file under `pcb_layout`.

**Board versus schematic.** After every route (and on load, on showing the
tab, or with *Check board against schematic*) the board is compared with the
schematic as it is now: connectivity is extracted from the copper geometry
alone (pads, tracks and vias that touch on a shared layer are one node,
whatever net the router labelled them), each pad is mapped to its part and
pin by the footprint's pin map, and every pin is compared. Opens (a schematic
net split on the board), shorts (copper joining different nets or a
not-connected pad), parts missing or extra, changed symbols and pins without
a pad are listed; the sidebar says MATCHES or DOES NOT MATCH. The pin maps
follow standard pinouts and must still be checked against the datasheets of
the parts used. Agent: `pcb_verify_netlist`. Code:
`Source/PcbLayout.*` (footprints, placement, route and check), drawn by
`PcbPanel`. The agent has `pcb_layout_get`, `pcb_footprints_list`,
`pcb_sync_from_schematic`, `pcb_auto_place`, `pcb_place_part`,
`pcb_set_footprint`, `pcb_set_route_rules`, `pcb_route`, `pcb_clear_routes`
and `pcb_drc`.

## Analytics (SPICE)

The **Analytics** button (toolbar), the **Analytics** menu, or any agent
analytics run opens the Analytics window, a large window of its own (it can
be docked back as a tab). Every analysis runs on the open diagram exactly as
it is and never changes a part:

| Analysis | What it gives |
| --- | --- |
| Operating Point (.OP) | node voltages, element currents and power, sources, per-device region and gm / r_pi / r_o / C_diff |
| DC Sweep (.DC) | any source, part value or temperature swept, optionally nested |
| AC / Bode (.AC) | magnitude, phase, group delay; peak, -3 dB corners, 0 dB crossing |
| Transient (.TRAN) | sine, square, pulse, PWL and exponential sources; corners hit exactly |
| Fourier / THD (.FOUR) | harmonics, normalized levels and phases, THD |
| Noise (.NOISE) | output and input-referred density, integrated rms, per-element contributions |
| Transfer Function (.TF) | small-signal gain, input and output resistance |
| Sensitivity (.SENS) | DC output or AC gain change per +1 % of every parameter |
| Pole-Zero (.PZ) | poles, zeros, damping, Q, s-plane plot (signed-log axes) |
| Temperature Sweep (.TEMP) | operating point across temperature, drift per degree |
| Monte Carlo / Tolerance | distribution, mean, sigma, worst case of a chosen result |

AC, transient, noise and DC sweeps take a parametric step (.STEP) over any
part parameter or temperature. Plots have two cursors with deltas, drag and
wheel zoom, and a legend that hides/solos traces; measurements (.MEAS: rise
time, overshoot, settling, RMS, -3 dB, unity gain, phase/gain margin...) are
added under the plot. Every run is kept in the history and written as CSV
plus `result.json` to `outputs/<diagram>/analytics/<time>_<analysis>/`.

The engine is `Source/Analytics.*` on the `CircuitSolver` math pack (MNA:
Newton with SPICE junction limiting, gmin and source stepping; trapezoidal
transient with step halving; small-signal Y(s) = G + sC for AC, noise, TF,
sensitivity and pole-zero) and `SignalMeasure` (measurements). The agent has
one tool per analysis (`analytics_operating_point` ... `analytics_monte_carlo`)
plus `analytics_list`, `analytics_measure`, `analytics_result` and
`analytics_open`, built from the same field tables as the window's forms.
`tools/solver_tests` checks every analysis against hand-calculated values.

Device models: BJTs are Ebers-Moll with Early voltage, transit time and
junction capacitances; diodes have Cj0 and transit time; op amps are a linear
gain stage, a dominant pole at GBW / A0, and a rail-limited output stage;
models scale with temperature (SPICE Is(T), Vt(T), resistor tempco, MOSFET
Vth/K). Not modelled yet: op amp noise, slew rate and output resistance,
flicker noise, logic gates.

`File > Preferences...` opens a searchable preferences window (Layout,
Display, Units, Projects). Settings are stored in
`%APPDATA%\DjehutiElectronicsLab\preferences.json` and take effect at once.
Layout options include supply symbols vs. kept rails, instrument net labels,
vertical chain stacking, supply block vs. left column, spacing, wire style and
wire gap. The agent reads and changes them with `preferences_list` and
`preferences_set`.

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

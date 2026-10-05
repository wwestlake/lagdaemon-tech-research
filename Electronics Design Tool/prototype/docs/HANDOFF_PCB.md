# Handoff: PCB designer (board, parts, routing, verification, fab files)

Date: 2026-10-05. App: Djehuti Electronics Lab (`prototype/`), JUCE C++,
Windows only. Router library: DjehutiRoute (`D:\DjehutiRoute`, MIT,
`github.com/wwestlake/DjehutiRoute`), linked by `add_subdirectory`.

Read `D:\000 Tech Research\AGENTS.md` first (no demos, solver-test rule,
Debug single-core builds, commit + push routine). This document is the state
of the PCB work at handoff; the code is the ground truth if they disagree.

## What works now (all verified, numbers below)

The PCB tab, end to end, on any diagram:

1. **Board** - standard boards (Eurocard sizes, Pi HAT, Arduino Uno envelope,
   credit card, 100 x 100 / 50 x 50), parametric shapes (rectangle, rounded,
   chamfered, L, U, T, circle, regular polygon) or any drawn straight-edged
   outline; mounting holes, cutouts, 1/2/4/6/8 layers, thickness, edge
   clearance.
2. **Parts** - *Update parts from schematic* puts every schematic part on the
   board as a footprint carrying the schematic's nets (full nets, the same
   names Analytics shows). Instruments and ideal controlled sources stay off.
   First sync auto-places everything; later syncs keep placed parts. Drag,
   R / right-click rotates, footprint choice in the sidebar.
3. **Routing** - *Route board* runs DjehutiRoute (grid A*, negotiated
   congestion, vias) on a worker thread; results apply only if nothing changed
   meanwhile. Exact design-rule check afterwards; violations marked on board.
4. **Board versus schematic** - connectivity extracted from the copper
   geometry alone, pads mapped to pins by footprint pin maps, compared pin by
   pin with the live schematic (opens, shorts, missing/extra parts, symbol
   changes, unmapped pins). Runs after every route, on load, on tab show.
5. **Text and graphics** - text (vector stroke font) and line / rect / circle /
   arc / polygon (outlined or filled) on F/B silkscreen or F/B copper; bottom
   mirrored; copper art is a router keepout and DRC-checked.
6. **Fab files** - Gerber X2 (copper per layer, mask, silk, paste, profile),
   Excellon PTH/NPTH, Gerber job file, IPC-D-356A netlist, pick-and-place and
   BOM CSVs, zip; every Gerber / drill / IPC file is read back and compared
   object by object with the board; readiness (routed, DRC clean, matches
   schematic) reported separately.

Everything a user can do has an agent tool (parity rule): `pcb_board_*`,
`pcb_layout_get`, `pcb_footprints_list`, `pcb_sync_from_schematic`,
`pcb_auto_place`, `pcb_place_part`, `pcb_set_footprint`,
`pcb_set_route_rules`, `pcb_route`, `pcb_clear_routes`, `pcb_drc`,
`pcb_verify_netlist`, `pcb_text_add/edit/remove`, `pcb_graphic_add/edit/remove`,
`pcb_set_fab_rules`, `pcb_export_fab`. Cards in
`knowledge/cards/electronics_tool_cards.jsonl`.

## Code map

| File | Role |
| --- | --- |
| `Source/PcbBoard.*` | board outline, holes, cutouts, stackup; JSON key `pcb` |
| `Source/PcbLayout.*` | footprints + per-symbol pin maps, parts, placement, route/DRC bridge to DjehutiRoute, ratsnest, board-vs-schematic check; JSON key `pcb_layout` |
| `Source/PcbFont.*` | single-stroke vector font (cap 6 units, advance 5.5) |
| `Source/PcbArtwork.*` | text/graphic strokes, part silkscreen, mask/paste openings, silk clear areas, copper-art keepouts and checks, fab-limit warnings |
| `Source/PcbFab.*` | Gerber X2 / Excellon / job / IPC-D-356A / CSV writers, and readers used for the read-back check |
| `Source/PcbPanel.*` | the tab: canvas (draw, drag, rotate, art tools, silk preview clipped like the Gerber) and sidebar |
| `Source/ElectronicsWorkbench.cpp` | `pcbParts()` on the schematic panel; save/load; `pcbTool` (board) and `pcbLayoutTool` (everything else); `agentTools.longTool` runs `pcb_route` asynchronously for the local API and the BYOK agent |

Data lives in the diagram file (`Documents\Djehuti Electronics Lab\Projects\
<project>\diagrams\<diagram>.diagram.json`): schematic (`components`,
`wires`...), board (`pcb`), parts/rules/texts/graphics/fab rules/routed
copper (`pcb_layout`). The refdes is the link between schematic and board.
Fab output: `<project>\outputs\<diagram>\fab\`.

## Verification record (SPICE Full Test Bench, L board 80 x 60 less 30 x 25, three M3 holes, 4 layers)

Hand-predicted before each run, then confirmed through the agent API:

- 23 parts, 14 nets, 0 placement problems; route 36/36, 6 vias, 232.7 mm,
  0 DRC violations; independent re-check of saved copper 0.
- Board vs schematic: match, 53 pins (50 + U1's 3 NC pads), 14 nets.
  Copy with +12V joined to GND in the schematic: exactly 1 open.
  Copy with CE.2's only ground symbol deleted: exactly 1 short.
- Fab export: PTH 14 holes (8 header 1.0 mm + 6 vias 0.3 mm), NPTH 3 x 3.2 mm,
  F_Mask 53 / B_Mask 8 openings, F_Paste 45, Edge_Cuts 6 edges, IPC 53 test
  points, BOM 17 lines for 23 parts, job file lists 12 files; all read back
  equal to the board.
- Art: "DJEHUTI LAB" 1.5 mm / 0.2 mm line measured 14.95 x 1.70 mm (hand:
  14.75 + 0.2); B.Cu "REV A" added exactly 16 NonConductor strokes to B_Cu
  (R 7 + E 4 + V 2 + A 3); reroute around it 36/36, 0 violations, matches.
- UI stayed responsive during a ~20 s Debug route (Windows "responding"
  check at every 1 s sample).

**Not yet done: an independent viewer check.** The read-back check proves the
files say what the board says, using our own reader. Open
`SPICE_Full_Test_Bench-fab.zip` in KiCad GerbView
(`D:\Program Files\KiCad\bin\gerbview.exe`) or a fab's online Gerber viewer
before sending money to a fab. The user declined GUI access for GerbView in
this session, so this is the user's step.

## How to test through the API (no screenshots needed)

The app writes `%APPDATA%\DjehutiElectronicsLab\agent-api.json` (base URL +
bearer token). `POST {baseUrl}/v1/tools/call` with
`{"name": "...", "arguments": {...}}`. A failing tool returns HTTP 409 with
the JSON in the body (PowerShell 5.1: catch and read `ErrorDetails.Message`).
The local API handles one request at a time, so a long `pcb_route` queues
other calls behind it - that is not the UI freezing.

## Gotchas learned the hard way

- **One app copy only.** The app is single-instance; starting the exe again
  brings the running window forward. Do not use the computer-use
  `open_application` to start it (it used to spawn copies). Close the app
  (`Stop-Process -Name djehuti_electronics_lab`) before every build - the exe
  is locked while it runs - and remember unsaved diagram state is lost.
- **Selection-based tools.** `deleteSelected()` prefers the UI
  multi-selection, and loading a diagram selects component 0. The agent's
  `schematic_delete_components` therefore deleted VCC instead of the named
  part until fixed (commit 65c220f). Any new agent tool must act on what it
  names, never on the UI selection.
- **Builds**: Debug, single core, scoped target:
  `cmake --build build --config Debug --target DjehutiElectronicsLab -- /m:1 /v:minimal`.
  ElectronicsWorkbench.cpp is ~14k lines; a build touching it takes ~10 min.
- **Footprint pin maps are datasheet facts** (SOT-23 1=B 2=E 3=C for MMBT3904,
  TO-92 E-B-C for 2N3904, 741 SOIC-8 2/3/4/6/7). Geometry checks cannot prove
  them; the board-vs-schematic check says so in every result.
- Auto-placement packs parts tightly around the board centre; it routes, but
  users will want to spread parts by hand.
- Default footprints are generic (e.g. a 100 uF capacitor gets 0805);
  check them per part before fab.

## Open items / next steps (none started)

1. Independent viewer check of the fab zip (user, GerbView or fab viewer).
2. `schematic_disconnect` agent tool (parity gap: UI can delete a wire, agent
   cannot) - offered as a separate task chip.
3. Copper pours / ground planes (zone fill with thermal reliefs) - not built.
4. Logo import from an image file to silkscreen - not built.
5. ODB++ / IPC-2581 single-file outputs - not built (Gerber + Excellon is
   accepted everywhere).
6. Better auto-placement (spread, keep decoupling caps near IC power pins).
7. Per-net track widths (power nets wider) - the router supports
   `Net::widthOverride` and IPC-2221 width helpers; not exposed in the app.
8. Bottom-side parts (everything is placed on top today).
9. Old Gemini files in DjehutiRoute (`autorouter`, `constraints`,
   `cost_function`, `design_advisor`, `grid`, `industrial`) are unbuilt and
   still in the repo; deleting them was blocked by the harness - ask the user.
10. Realtime circuit simulation through the embedded Frust compiler (the
    user's earlier goal, parked for the router/PCB work).

## Commits (this work)

Electronics Lab (`D:\000 Tech Research`, branch master): c0dd80e PCB tab
(board), a805c3e single instance, aaad3f7 auto-fit, df437ce parts + routing,
65c220f board-vs-schematic + delete fix, and the text/graphics/fab-files
commit that follows this document. DjehutiRoute: 9810658 router rebuild,
b64eb3d outlines/cutouts/standard boards.

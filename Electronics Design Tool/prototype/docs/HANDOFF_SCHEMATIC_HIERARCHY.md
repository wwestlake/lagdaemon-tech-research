# Handoff: Schematic Engine, Agent Parity, Group Boxes, Sub-Diagrams

Written 2026-10-04 at the end of a long session so a fresh session can pick
up cleanly. Code is the source of truth; verify anything here against it.

## Where things stand

### Layout and routing engine (done, commit `ab0fa67`)

- `Source/SchematicLayout.cpp` - connectivity-driven placement. Layers left
  to right from input sources; each part aligns its input pin with the net
  feeding it; two-pin shunts stand vertically off their net (ground and
  negative supplies down, positive up); parts fed by a lone vertical pin stack
  in line with it (textbook push-pull column); DC supplies go to a supply
  block; instruments go right and connect through `SCOPE1.CH1`-style net
  labels; every ground/supply pin gets its own ground symbol or named port.
- `Source/SchematicRouter.cpp` - all libavoid use. One `Avoid::Router` per
  diagram, symbols as shapes with a `ShapeConnectionPin` per pin, every wire a
  connector so nudging separates nets. Auto layout routes multi-pin nets
  through `HyperedgeRerouter` (libavoid picks junctions), with a
  minimum-spanning-tree fallback. Routes are grid snapped, conflicts resolved,
  junctions slid onto their real T.
- `Source/SchematicSymbols.cpp` - symbol geometry and art, grid-aligned pins,
  `pinLeadDirection`, `extentBounds`, `labelRectsFor`, `net_label` symbol.
- Canvas (`ElectronicsWorkbench.cpp`): `autoLayoutInstances` rebuilds net
  markers, junctions and wires from the layout; `ensureRoutes` caches
  whole-diagram routes keyed by a geometry signature.
- `third_party/libavoid/hyperedge.cpp` carries a documented local patch.
- Rules the engine follows: `docs/SCHEMATIC_SYMBOL_STANDARDS.md` (SCH-*).

Verified headlessly with the preview tool and live in the app (the in-app
agent built the push-pull and ran `schematic_auto_layout`).

### Headless preview tool

```text
cmake --build build --config Debug --target SchematicPreview -- /m:1
bin\Debug\schematic_preview.exe <output-folder>
```

Renders reference circuits (RC low-pass, push-pull, inverting op amp, common
emitter, battery LED) to PNG and prints crossings, overlaps, body hits, bends,
unrouted wires and off-grid points. Set `SCHEMATIC_PREVIEW_DEBUG=1` for per-net
tree output. All metrics are zero today; keep them zero. Add a reference
circuit whenever a real circuit lays out badly.

Run it with `Start-Process ... -RedirectStandardOutput` and a timeout; it
reports assertion failures on stderr instead of dialogs.

### Talking to the in-app agent

While the app runs it writes `%APPDATA%\DjehutiElectronicsLab\agent-api.json`
(`baseUrl`, `token`; both change every launch). Use that API to drive the
agent instead of the UI:

- `GET /v1/status`
- `POST /v1/messages` with `{"content": "..."}` returns a `requestId`
- `GET /v1/requests/<id>` to poll for `completed` / `failed`
- `POST /v1/session`, `POST /v1/cancel`, `GET /v1/plan`,
  `POST /v1/plan/approve`, `POST /v1/plan/deny`

- `POST /v1/tools/call` with `{"name": "<tool>", "arguments": {...}}` runs
  one agent tool directly, no model in the loop. Use this for testing:
  gpt-4o-mini sometimes skips requested tool calls and reports results it
  never got.

Header: `Authorization: Bearer <token>`. Never print the token. Each message
costs the user's OpenAI key a little; do not spam it.

## Rule: agent parity

Anything a user can do in the app, the agent can do through a tool: create,
change, edit, delete. A UI feature is not finished until its agent tool,
tool description, and tool card (`knowledge/cards/electronics_tool_cards.jsonl`)
exist. This rule is also in the prototype README and the repo `AGENTS.md`.

## Agreed plan (in order)

Every feature ships together with its agent tool(s) and tool card; there is
no separate "agent tools" pass.

1. **Group boxes - done (`f8a5658`).** Box around members' full footprints
   with a name tab in the top-left corner; create from selection prompts for
   the name; agent tools `schematic_group_create/update/delete/list`.
   Known gap: auto layout does not yet keep a group's members together.
2. **Sub-diagrams - done.** Sheets on the flat model (`Instance::sheet`,
   `junctionSheets`), `sub_block` with per-instance pins from its ports,
   `block_port` bubbles joined to block pins by name in `computeNetNames`.
   Collapse (`createSubDiagram`), expand (`expandSubDiagram`, leaves a group
   box), double-click / breadcrumb / Backspace navigation, nesting, save and
   load. Agent tools `schematic_subdiagram_create/expand/open/rename/
   rename_port/list`, plus `schematic_export_image` and `project_save/open/
   new`. Verified with direct tool calls (below); ERC passes after a full
   collapse / nest / save / load / expand round trip. Expanding keeps the
   inner positions; run auto layout afterwards for a clean sheet.
   **Sub-diagram details.** Purely a view of one flat circuit, for readability:
   - A selection or group collapses into a small named block. Every wire that
     crossed the boundary becomes a pin on the block; parent wires connect to
     those pins.
   - Double-click opens the block's own diagram. Inside, every external
     connection is drawn as a **port bubble**: a rounded bubble with the
     name of the block pin it maps to (IN, OUT, +12V...) written inside it,
     wired to whatever it reaches inside. (User's call: a bubble with the
     pin name.) Port symbols and
     block pins are one-to-one; renaming one renames the other. Ports sit at
     the diagram edge (inputs left, outputs right, supplies top/bottom) and
     must look different from net labels so it is obvious they leave the
     block. Breadcrumb (Main > Power Amp > Output Stage) goes back up. Auto
     layout works inside a block and treats ports as its inputs/outputs.
   - Nestable to any depth.
   - Netlist, ERC and simulation always see the flat circuit; blocks never
     change what the circuit is. They are not reusable definitions (not KiCad
     sheets or SPICE `.SUBCKT`).
   - Group box vs block: a group box is a labelled region whose wires go
     straight to the visible parts; a block is the same selection collapsed
     with pins. Expanding a block can return it to a group box.
   - Agent tools: create, open, close/up, rename, expand, list, and work
     inside a block.
3. **Save and load** the block hierarchy in `circuit.json` (groups already save).
4. Agent tools for remaining existing features (delete, move, rotate, rename,
   select, save/open/new) as those areas are touched.

Discuss real design choices with the user before building (see repo
`AGENTS.md`); agree each milestone in words, then carry it through.

## Known rough edges in the engine

- Selection-scoped auto layout only moves parts; it does not rebuild markers
  or wiring for the selection.
- `ensureRoutes` reroutes the whole diagram on every geometry change,
  including every mouse move while dragging. Fine for small diagrams;
  consider libavoid's incremental `moveShape` if large diagrams get slow.
- Net-label text is not included in label footprints, so a long label can
  touch a neighbour.
- A junction dot occasionally sits one grid step off its T (seen once on the
  op-amp inverting node in the preview).
- `projects/current/` is gitignored; saved diagrams are not in the repo.

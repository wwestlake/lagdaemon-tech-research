# Schematic Symbol Standards

The Electronics Design Tool should treat schematic symbols as product-track
infrastructure, not demo artwork.

## References

- IEC 60617, Graphical Symbols for Diagrams. IEC describes this as the
  electrotechnical graphical-symbol database, with more than 1500 symbols.
- IEEE/ANSI 315-1975, Graphic Symbols for Electrical and Electronics Diagrams.
  This remains a common electronics-symbol reference, including reference
  designation letters.
- KiCad generic symbol libraries and library conventions. KiCad is not the
  governing standard, but it is a practical open EDA reference for generic
  symbol coverage, naming, pin roles, and library organization.

## Product Rule

The app must not silently substitute one symbol class for another. If an agent
asks for an unsupported symbol, the placement tool must fail loudly and name
the unsupported symbol. A resistor must never become a fake transistor,
switch, source, or integrated circuit.

## Coverage Model

The generic symbol set is the first layer. It covers schematic component
classes such as passives, semiconductors, sources, controlled sources,
switches, connectors, transformers, logic gates, instruments, and protection
devices.

Specific manufacturer parts, package pinouts, footprints, SPICE models, and
procurement data are a second layer. Those must bind onto generic symbols
without changing the schematic meaning.

## Source Availability

IEC 60617 and IEEE 315 are paid standards; neither is vendored here. The
rules below are the drawing conventions those standards and common EDA
practice (KiCad library conventions, textbook/datasheet schematics) agree
on, written down as checkable rules. They are the contract for hand
editing, auto layout, routing, and agent-generated schematics alike.

## Drawing Rules

Each rule is numbered so validators, tests, and agent tool results can
cite it (`SCH-G1`, `SCH-W3`, ...).

### Grid

- **SCH-G1** The schematic grid is 24 px. Every pin end, wire vertex,
  junction, and symbol origin lies on it.
- **SCH-G2** Symbol definitions place every pin end on the grid relative
  to the symbol origin, in all four rotations. A symbol whose pins are
  off-grid is a library bug, not something the router compensates for.

### Wires

- **SCH-W1** Wires are orthogonal: horizontal and vertical segments only.
- **SCH-W2** A wire never passes through a symbol body, and never touches
  a pin it is not connected to.
- **SCH-W3** Wires of different nets never overlap along a segment and
  never run closer than one grid step in parallel.
- **SCH-W4** Wires connect only at pin ends or junctions, never to the
  middle of a symbol edge.
- **SCH-W5** Prefer the fewest bends and crossings. A crossing of two
  wires with no dot means no connection.

### Junctions

- **SCH-J1** A connection of three wires (a T) is marked with a filled
  junction dot.
- **SCH-J2** No four-way junctions. A node with four branches is drawn as
  two T junctions offset by at least one grid step, so a dot can never be
  mistaken for a crossing or the other way round.
- **SCH-J3** No junction dot where only two wires meet, or at a plain
  pin-to-wire connection.

### Power and Ground

- **SCH-P1** Ground is drawn as a ground symbol at each ground connection,
  pointing down, not as a long wire back to one shared ground symbol.
- **SCH-P2** Supply nets (VCC, +15V, -15V, ...) are drawn as named
  power-port symbols at each connection: positive supplies point up,
  negative supplies point down. Ports with the same name are the same net.
- **SCH-P3** Higher potential is at the top of the page, lower potential
  and ground at the bottom; conventional current flows downward.

### Net Labels

- **SCH-L1** Net labels with the same name are the same net, like supply
  ports. Instrument inputs (scope channels, meter leads) connect through a
  pair of labels named `<instrument>.<input>` (for example `SCOPE1.CH1`): one
  at the instrument, one on the probed net. Probe leads are never drawn as
  wires across the circuit.

### Sub-Diagrams

- **SCH-H1** A sub-diagram block is a view of part of the one flat circuit,
  not a reusable definition. Each signal net crossing its boundary is one
  block pin (inputs left, outputs right); inside, the same net ends at a
  port bubble carrying the pin name. Ground and named supplies are global
  symbols and never become pins. Blocks nest; reference letter `A`.

### Placement and Flow

- **SCH-F1** Signal flows left to right: sources and inputs on the left,
  processing in the middle, loads and outputs on the right, instruments on
  the far right.
- **SCH-F2** Symbol bodies and their labels never overlap each other or
  any wire. Default minimum spacing between symbol centers is 144 px
  horizontally or 96 px vertically.
- **SCH-F3** Two-pin parts are oriented along the wire they sit in: a
  series element in a horizontal signal path is horizontal; a shunt
  element to ground or a supply is vertical.
- **SCH-F4** Placement follows connectivity, not part type. Parts that
  share a net are placed near each other; recognized building blocks use
  their textbook arrangement (for example, a complementary push-pull pair
  is drawn as NPN above PNP with emitters meeting at the output node and
  the bias network between the bases).
- **SCH-F5** Transistors are drawn with the collector or drain toward the
  higher potential: an NPN or NMOS has its emitter or source down, a PNP
  or PMOS has its emitter or source up.

### Text

- **SCH-T1** Every part shows its reference designator and value next to
  the body, reading left to right (never rotated with the part).
- **SCH-T2** Labels never overlap symbols, wires, or other labels.
- **SCH-T3** Reference designators follow IEEE 315 / common EDA letters:
  R resistor, C capacitor, L inductor, D diode (including zener, Schottky,
  LED), Q bipolar or field-effect transistor, U integrated circuit or op
  amp, T transformer, K relay, F fuse, SW switch, J connector, BT battery.
  Independent sources use the SPICE letters V and I so the schematic and
  netlist agree.

## Implementation

`Source/SchematicLayout.cpp` (placement) and `Source/SchematicRouter.cpp`
(libavoid routing) implement these rules; `tools/schematic_preview` renders
reference circuits to PNG and reports crossings, overlaps, wires through
symbols, and off-grid points so layout changes can be checked headlessly:

```text
bin\Debug\schematic_preview.exe <output-folder>
```

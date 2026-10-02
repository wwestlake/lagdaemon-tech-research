# Xyce Backend Plan

## Goal

Make Xyce the first reference SPICE-class backend for the electronics design tool research prototype.

## First Supported Flow

```text
Electronics graph JSON
-> circuit IR
-> Xyce netlist
-> run Xyce as external process
-> parse output
-> normalized dataset JSON
-> instrument display
```

## Minimal Circuit Elements

First pass:

- ground node
- resistor
- capacitor
- inductor
- independent DC voltage source
- independent DC current source
- voltage probe

Second pass:

- transient voltage source
- sine/pulse sources
- diode
- BJT/MOSFET via Xyce model references
- op amp via macro-model/subcircuit

## Output Dataset Shape

```json
{
  "id": "run_001",
  "backend": "xyce",
  "analysis": "dc",
  "netlist": "path/to/generated.cir",
  "signals": [
    {
      "name": "V(out)",
      "domain": "dc",
      "unit": "V",
      "samples": [
        { "x": 0.0, "y": 5.0 }
      ]
    }
  ],
  "diagnostics": []
}
```

## Integration Rules

- The app must not hard-code Xyce paths.
- Local machine paths live in `config/xyce.local.json`.
- Generated netlists belong under `sim/xyce/runs`.
- Generated circuit artifacts are described in `GENERATED_ARTIFACTS.md`.
- Solver output must be converted to app datasets before instruments consume it.
- Circuit/source maps should preserve component and net origins so diagnostics can point back to schematic objects.

## Open Questions

- Preferred Xyce output format for robust parsing: `.PRINT` text, CSV, or another supported writer.
- Best Windows install/discovery flow.
- How much Xyce model syntax should the component database expose directly.
- How op amp macro-models should be attached to component records.

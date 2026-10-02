# Generated Circuit Artifacts

The schematic editor treats the graphical circuit model as the source of truth.
Exports are generated from that model for backend tools.

Current generated files:

- `runs/generated/circuit.json` - authoritative circuit graph snapshot for the current schematic.
- `runs/generated/generated.cir` - lowered Xyce netlist produced from the circuit JSON.

The Xyce netlist is a backend artifact, not the editable project format. As the
tool grows, diagnostics from Xyce should be mapped back through the JSON model so
the UI can point at the original component, pin, wire, or net.

The current lowerer supports a first slice:

- ground nets
- resistors
- capacitors
- independent DC voltage sources
- operating-point print output

Unsupported symbols are preserved as comments in the generated netlist until
their component records and lowering rules exist.

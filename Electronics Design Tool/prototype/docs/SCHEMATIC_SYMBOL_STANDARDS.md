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

## Layout Rule

Generated schematics should use signal flow left-to-right, sources on the
left, processing components in the middle, loads or outputs on the right,
grounds below their related components, and instruments to the far right.
Symbol placements should not overlap; as a default, leave at least 144 px
horizontal or 96 px vertical spacing between symbol centers.

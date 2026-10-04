# Third-Party Layout And Routing

The Electronics Design Tool vendors `libavoid` from the Adaptagrams project under
`third_party/libavoid`.

`libavoid` is the foundation for obstacle-aware orthogonal connector routing so
schematic wires and instrument leads route around components and group boxes
instead of being drawn as simple direct segments. Routes must satisfy the wire
rules (`SCH-W*`) in `SCHEMATIC_SYMBOL_STANDARDS.md`.

Usage: `Source/SchematicRouter.cpp` owns all libavoid use. One `Avoid::Router`
holds the whole diagram: every symbol is a `ShapeRef` sized to its full
footprint (body plus pin ends) with a `ShapeConnectionPin` at each pin end,
every wire is a `ConnRef` in that same router so orthogonal nudging can space
different nets apart, and auto layout routes nets of three or more pins
through `HyperedgeRerouter` so libavoid chooses the junctions. Routes are
snapped to the 24 px grid afterwards.

Local patch: `hyperedge.cpp` `HyperedgeRerouter::newAndDeletedObjectLists`
asserted `index <= count()`, but `performRerouting()` clears the terminal list
that `count()` reads, so every hyperedge after the first failed the assert.
The check now tests the result vectors instead. Keep this patch if libavoid
is re-vendored.

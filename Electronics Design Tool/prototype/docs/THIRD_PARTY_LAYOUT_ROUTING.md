# Third-Party Layout And Routing

The Electronics Design Tool vendors `libavoid` from the Adaptagrams project under
`third_party/libavoid`.

`libavoid` is the foundation for obstacle-aware orthogonal connector routing so
schematic wires and instrument leads route around components and group boxes
instead of being drawn as simple direct segments. Routes must satisfy the wire
rules (`SCH-W*`) in `SCHEMATIC_SYMBOL_STANDARDS.md`.

Status (2026-10-03): vendored and compiled into the app target, but not yet
called by the schematic canvas. Wires are still drawn as independent L-shaped
paths. Wiring libavoid into the router is the next schematic task.

Licensing:

- Upstream project: https://github.com/mjwybrow/adaptagrams
- Library documentation: https://www.adaptagrams.org/documentation/libavoid.html
- Vendored license: `third_party/libavoid/LICENSE.LGPL`

The temporary upstream checkout path `third_party/adaptagrams-src/` is ignored
and should not be committed.

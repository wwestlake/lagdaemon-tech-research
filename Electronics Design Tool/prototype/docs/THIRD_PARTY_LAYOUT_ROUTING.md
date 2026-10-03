# Third-Party Layout And Routing

The Electronics Design Tool vendors `libavoid` from the Adaptagrams project under
`third_party/libavoid`.

`libavoid` is used as the foundation for obstacle-aware orthogonal connector
routing so schematic wires and instrument leads can route around components and
group boxes instead of being drawn as simple direct segments.

Licensing:

- Upstream project: https://github.com/mjwybrow/adaptagrams
- Library documentation: https://www.adaptagrams.org/documentation/libavoid.html
- Vendored license: `third_party/libavoid/LICENSE.LGPL`

The temporary upstream checkout path `third_party/adaptagrams-src/` is ignored
and should not be committed.

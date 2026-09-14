# Frust Linear Algebra Math Pack R&D Spec

## Purpose

`frust_linalg` is the first serious math pod for Frust: a reusable Frate
library for game, graphics, DSP, simulation, and realtime systems code.

The guiding constraint is practical usefulness over abstraction theater.
Version 0.1 should give programmers excellent fixed-shape math types and
operations that work with Frust as it exists today, especially the new
fixed-size `Array<T, N>` feature.

## Package Shape

The package is a Frate lib pod:

```frust
use frust_linalg;
```

The dependency version belongs in `frate.json`, not in source. Fine-grained
imports such as `use frust_linalg::Vec3f;` are future language work.

## V1 Capability Set

### Scalar Helpers

- `abs_f32`
- `min_f32`
- `max_f32`
- `clamp_f32`
- `lerp_f32`
- `near_zero_f32`
- `approx_eq_f32`

### Fixed Vectors

Types:

- `Vec2f`
- `Vec3f`
- `Vec4f`

Capabilities:

- constructors
- zero values
- component-wise add/subtract
- scalar multiply
- dot product
- cross product for `Vec3f`
- squared length
- distance squared
- component-wise min/max/clamp
- linear interpolation

Length/normalize are intentionally deferred until the math pod cleanly
depends on the standard `core` math functions or the compiler exposes
`sqrt` in a stable import path.

### Matrices

Types:

- `Mat4f`

Capabilities:

- zero
- identity
- translation
- scale
- matrix multiply
- transform point
- transform direction
- transpose

Perspective, orthographic, look-at, determinant, and inverse are planned
for later. They are useful, but v1 should first validate source import,
struct layout, fixed array storage, and basic arithmetic.

### Realtime Buffers

Types:

- `AudioBlock256f`

Capabilities:

- clear/fill
- gain
- mix
- dot product
- sum of squares
- peak absolute value

This intentionally uses `Array<f32, 256>` directly so the package exercises
the exact fixed-buffer feature needed for audio/DSP and realtime code.

## Future Capability Map

- `Quatf`
- `Transform3f`
- `Ray3f`, `Plane3f`, `Aabb3f`, `Spheref`
- matrix inverse/determinant
- projection and camera helpers
- `Vector<T, N>` and `Matrix<T, R, C>` once const-generic arithmetic is
  strong enough
- window functions and simple DSP filters
- fine-grained imports and namespacing

## Test Strategy

Every test pod is executable and returns a hand-predicted integer summary:

- vector smoke test: validates vector constructors, dot/cross, add/sub,
  scalar multiply, lerp, clamp
- matrix smoke test: validates identity, translation, scale, multiply,
  point/direction transforms
- audio buffer smoke test: validates fixed array storage, bounds-safe
  indexed writes, gain/mix/dot/sum-of-squares/peak

Floating-point tests use integer-exact values where possible so the result
can be checked by process exit code without formatting/parsing.

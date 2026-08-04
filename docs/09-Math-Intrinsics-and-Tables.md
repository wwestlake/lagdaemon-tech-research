# 09 - Hardware Math Intrinsics & Compile-Time Lookup Tables

In **Frust**, high-performance math is designed for bare-metal OS kernels, audio DSP engines (JUCE), and 3D graphics.

---

## 1. Hardware SIMD Primitives

Frust provides direct hardware SIMD register mapping (`v128f`, `v256f`, `v512f`):

```frust
type v128f = Simd<f32, 4> // 128-bit hardware vector register

pub fn dot_product_4d(a: v128f, b: v128f) -> f32 = {
    let mul = intrinsic::simd_mul(a, b)
    return intrinsic::simd_reduce_add(mul)
}
```

---

## 2. Bare-Metal Compile-Time Lookup Tables (`build_time` LUTs)

To avoid slow `libc` dependencies or non-deterministic floating point calls in bare-metal environments, lookup tables (sine, cosine, exp, fixed-point DSP) are **computed at compile-time** using `build_time` blocks and embedded as read-only static memory in the binary:

```frust
// Computed entirely during compilation via OrcJIT!
pub static SINE_LUT: Array<f32, 2048> = build_time {
    generate_sine_table(2048)
}

pub fn fast_sin(phase: f32) -> f32 = {
    let idx = (phase * 2048.0) as usize % 2048
    return SINE_LUT[idx]
}
```

---

## 3. High-Level Math Types (`std::math`)

* **`Complex<T>`**: Complex numbers (`real`, `imag`) for FFTs and signal processing.
* **`Vec2` / `Vec3` / `Vec4`**: SIMD-backed N-dimensional vectors.
* **`Mat2x2` / `Mat3x3` / `Mat4x4`**: SIMD-backed matrices.
* **`Quaternion`**: 3D rotation math.

# 06 - Datatypes, Standard Library & Packaging (`frate`)

## 1. Dual Compilation Engine: JIT + Bare-Metal AOT ("Hard Iron")

Our LLVM architecture supports **both** execution modes from the exact same LLVM IR frontend:

* **JIT Mode (LLVM OrcJIT)**: Live in-memory compilation for dynamic execution, REPL loops, and JUCE app hot-swapping.
* **AOT Bare-Metal Mode (LLVM TargetMachine + LLD)**: Generates native `.obj` / `.o` machine code and links directly into standalone `.exe` (Windows) or ELF (Linux) binaries just like C or Rust!

---

## 2. Datatype System

### Numeric Primitives
* Signed: `i8`, `i16`, `i32`, `i64`, `isize`
* Unsigned: `u8`, `u16`, `u32`, `u64`, `usize`
* Floating Point: `f16`, `f32`, `f64`
* Text & Boolean: `bool`, `char`, `str`, `String`

### Sum & Product Data Types

```frust
// Sum Types (Algebraic Data Types)
type Shape =
    | Circle(f32)
    | Rectangle(f32, f32)
    | Point

// Monadic Types
type Option<T>  = | Some(T) | None
type Result<T,E> = | Ok(T)   | Err(E)

// Product Record
record Player {
    id: u64,
    name: String,
    position: (f32, f32)
}
```

---

## 3. Package Management & Build Tools (`frate`)

Cargo-style package management (`frate.toml` / `frate.lock`) managing module resolution, versioning, C/libc FFI bindings (`std::libc`), and target selection (`jit` vs `aot`).

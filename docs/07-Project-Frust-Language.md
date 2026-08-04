# 07 - Frust: Functional Rust Language Specification

**Frust** (**F**unctional **Rust**) is the official flagship programming language of **LagDaemon Software Research & Development**.

Combining the safety, module system, and bare-metal performance of Rust with expression-oriented functional programming, first-class verifiable components, and dual JIT/AOT compilation via LLVM.

---

## Language Highlights

* **File Extension**: `.frust` / `.fr`
* **Package Manager**: `frustpack` (`frustpack.toml`)
* **Modules & Visibility**: `mod`, `pub`, `use`, `as`, `::`
* **First-Class Components**: `interface`, `component`, `in`, `out`, `where`, `~>`
* **Types**: Sum Types (ADTs), Product Records/Tuples, Monads (`Option<T>`, `Result<T, E>`), Primitives (`i8`-`i64`, `u8`-`u64`, `f16`-`f64`).
* **Metaprogramming**: First-class `Type` values & `build_time` OrcJIT execution (No esoteric C++ templates).
* **Dual Compilation Engine**:
  * **JIT Mode**: LLVM OrcJIT in-memory execution for JUCE live hot-swapping & REPL loops.
  * **Hard Iron AOT Mode**: Native machine code generation linked via `LLD` into standalone `.exe` / ELF binaries.

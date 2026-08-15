# Project 01: Frust Multi-Paradigm Compiler Experiment

## Vision: Frust (Functional Rust)
Exploring a revolutionary multi-paradigm language architecture that unifies 4 major paradigms into a cohesive, JIT/AOT-backed LLVM language engine.

---

## Paradigm Integration Structure

### 1. Imperative & Memory Dimension (`01_imperative` & Smart Pointers)
* **Built-in Smart Pointers**: `own T` (unique owned), `shared T` (ref-counted), `weak T` (observer), `raw* T` (hardware/OS MMIO), `&T` (auto-borrowed managed ref).
* **No Lifetime Annotation Friction**: No `'a`, `'b` syntax overhead—compiler infers scope bounds automatically.
* **Control Flow Graphs**: Direct LLVM IR basic block generation with zero-overhead stack allocation (`alloca`).

### 2. Functional & Component Dimension (`02_functional`)
* **First-Class Verifiable Components**: `interface`, `component`, contractual invariants (`where`), and composable port wiring (`~>`).
* **Expression-Oriented Engine**: Immutable bindings, ADTs (Sum/Product types), Monads (`Option<T>`, `Result<T, E>`), and Tail-Call Optimization (TCO).

### 3. Meta Dimension (`03_meta`)
* **Intuitive Metaprogramming**: Types as first-class values (`Type`). Metaprogramming is plain code executed *during compilation* via LLVM OrcJIT (`build_time`).
* **AST Reflection & Code Synthesis**: First-class AST quote/unquote and type reflection without template bloat.

### 4. Declarative Dimension (`04_declarative`)
* **Reactive Dataflow & Signal Graph**: Declarative dependency nodes compiled into incremental evaluation loops (tailored for JUCE audio/visual node graphs and reactive UIs).

---

## Unified Execution Core (`common_ir_jit`)
Links all 4 dimensions into a dual-target backend:
1. **Dynamic JIT**: LLVM OrcJIT v2 for live REPL & JUCE app hot-swapping.
2. **Hard-Iron Bare-Metal AOT**: Native machine code emission linked via `LLD` for standalone `.exe` / ELF OS kernels and binaries.

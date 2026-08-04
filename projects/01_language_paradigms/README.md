# Project 01: Multi-Paradigm Compiler Experiment

## Vision: Beyond Bleeding Edge
Exploring a multi-paradigm language architecture that unifies 4 major paradigms into a cohesive, JIT-backed LLVM language engine.

---

## The 4 Core Paradigm Dimensions

### 1. Imperative Dimension (`01_imperative`)
* **Linear / Affine Resource Ownership**: Guarantees memory safety and deterministic cleanup without garbage collection or complex explicit lifetime annotations.
* **Control Flow Graphs**: Direct LLVM IR basic block generation with zero-overhead stack allocation (`alloca`).

### 2. Functional Dimension (`02_functional`)
* **Algebraic Effects & Handlers**: Replaces exceptions, async/await, generators, and Dependency Injection with a single, unified effect handling system.
* **Expression-Oriented Engine**: Immutable bindings, ADTs, pattern matching, and Tail-Call Optimization (TCO).

### 3. Meta Dimension (`03_meta`)
* **OrcJIT Compile-Time Execution**: Executes macro logic in-memory *during compilation* using LLVM OrcJIT.
* **AST Reflection & Code Synthesis**: First-class AST quote/unquote and type reflection.

### 4. Declarative Dimension (`04_declarative`)
* **Reactive Dataflow & Signal Graph**: Declarative dependency nodes compiled into incremental evaluation loops (tailored for JUCE audio/visual node graphs and reactive UIs).
* **Constraint & Rule Execution**: Declarative logic expressions compiled to optimized LLVM branching.

---

## Unified JIT Engine (`common_ir_jit`)
Links all 4 dimensions into a single runtime backed by **LLVM OrcJIT v2**, **Clang C Interop**, and **WebAssembly Code Emission**.

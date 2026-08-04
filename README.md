# LagDaemon Tech Research & Language Paradigm Lab

Public research repository for **LagDaemon Software Research & Development**.

This laboratory is dedicated to exploring programming language design, compiler backend architectures with LLVM (OrcJIT, WebAssembly, C Interop), and experimenting with multiple core programming paradigms.

---

## Workspace Structure

```
.
├── LICENSE                          # MIT License
├── README.md                        # Root Repository Documentation
├── RND_ROADMAP.md                   # Environment & LLVM Build Roadmap
├── build_llvm_wsl.sh                # Trimmed Linux LLVM 18 WSL build script
│
└── projects/
    └── 01_language_paradigms/       # Multi-Paradigm Compiler Experiment
        ├── 01_imperative/           # Procedural, state mutation, explicit control flow
        ├── 02_functional/           # Immutable expressions, pattern matching, closures
        ├── 03_meta/                 # Compile-time macros, reflection, AST transformation
        ├── 04_declarative/          # Dataflow, reactive streams, rule/constraint engines
        └── common_ir_jit/           # Shared LLVM OrcJIT execution & IR builder layer
```

---

## Research Focus Areas

### 1. Multi-Paradigm Compiler Design
Exploring how different language semantics translate into LLVM IR and interact with LLVM's ORCv2 JIT compiler:
* **Imperative**: Mutable bindings, stack frames, explicit control flow graph (CFG).
* **Functional**: Immutability, tail-call optimization (TCO), environment closures, expression evaluation.
* **Meta**: Compile-time evaluation, hygienic macro expansion, AST reflection & code generation.
* **Declarative**: Dependency graph evaluation, reactive dataflow, constraint solving.

### 2. LLVM Execution Environment
* **Primary Host JIT**: LLVM OrcJIT (ORCv2 API).
* **Target Architectures**: Native Host (`X86_64` / `AArch64`) + `WebAssembly` (`wasm32` / `wasm64`).
* **C ABI Interop**: Clang / libclang integration for header parsing and C function binding.

---

## License
[MIT License](file:///d:/000%20Tech%20Research/LICENSE) - Copyright (c) 2026 LagDaemon Software Research & Development.

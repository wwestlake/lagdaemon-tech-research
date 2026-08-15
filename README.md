# Frust (Functional Rust) & LagDaemon Tech Research Lab

Public research repository for **LagDaemon Software Research & Development**.

Home of **Frust** (**F**unctional **Rust**)—a modern, general-purpose, component-driven language built for dynamic embedded computing, JIT hot-swapping inside running hosts (e.g., JUCE desktop apps), and bare-metal AOT compilation via LLVM.

---

## Workspace Structure

```
.
├── LICENSE                          # MIT License
├── README.md                        # Root Repository Documentation
├── RND_ROADMAP.md                   # Environment & LLVM Build Roadmap
├── build_llvm_wsl.sh                # Trimmed Linux LLVM 18 WSL build script
│
├── docs/                            # Frust Language Specification & Wiki
│   ├── Home.md
│   ├── 01-Core-Vision.md
│   ├── 02-First-Class-Components.md
│   ├── 03-Rust-Like-Modules.md
│   ├── 04-First-Class-Metaprogramming.md
│   ├── 05-Grammar-and-Tokens.md
│   ├── 06-Datatypes-Std-and-Packaging.md
│   └── 07-Project-Frust-Language.md
│
└── projects/
    ├── 01_language_paradigms/       # Frust Language Core & Lexer
    │   ├── 01_imperative/           # Procedural Control Flow
    │   ├── 02_functional/           # Frust Spec, Modules, Datatypes & C++ Lexer
    │   ├── 03_meta/                 # Metaprogramming & Type Construction
    │   ├── 04_declarative/          # Reactive Dataflow Engines
    │   └── common_ir_jit/           # Shared LLVM OrcJIT & AOT Engine
    │
    └── 02_juce_language_host/       # JUCE Desktop Workbench Host (Local D:/JUCE + LLVM)
```

---

## License
[MIT License](file:///d:/000%20Tech%20Research/LICENSE) - Copyright (c) 2026 LagDaemon Software Research & Development.

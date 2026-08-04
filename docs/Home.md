# LagDaemon Language Suite - Official Specification & Research Wiki

Welcome to the official specification for the **LagDaemon Multi-Paradigm Language Suite**.

Designed by **LagDaemon Software Research & Development**, this language suite is built for **dynamic embedded computing**, **JIT hot-swapping inside running hosts (e.g. JUCE desktop apps)**, and **beyond-bleeding-edge programming abstractions**.

---

## Language Specification Index

1. **[Core Vision & Philosophy](01-Core-Vision.md)**
   * Embedded JIT computing model.
   * Moving beyond standard ML/C-style paradigms.
2. **[First-Class Verifiable Components](02-First-Class-Components.md)**
   * Components as first-class citizens.
   * Verifiable interfaces & contractual invariants (`where` bounds).
   * Composable port wiring (`~>`).
3. **[Rust-Like Module System](03-Rust-Like-Modules.md)**
   * Submodules, `pub` visibility, and import paths (`use`).
   * LLVM OrcJIT `JITDylib` hot-reloading architecture.
4. **[First-Class Metaprogramming & Type Construction](04-First-Class-Metaprogramming.md)**
   * Eliminating arcane C++ templates and generics.
   * Types as first-class values (`Type`).
   * `build_time` OrcJIT metaprogramming execution.
5. **[EBNF Grammar & Tokenizer](05-Grammar-and-Tokens.md)**
   * Formal EBNF syntax rules.
   * C++ Lexer implementation tokens.

---

*Repository*: [github.com/wwestlake/lagdaemon-tech-research](https://github.com/wwestlake/lagdaemon-tech-research)
*License*: MIT License

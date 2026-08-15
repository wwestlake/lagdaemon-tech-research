# Frust (Functional Rust) - Official Specification & Research Wiki

Welcome to the official specification for **Frust** (**F**unctional **Rust**)—the flagship programming language designed by **LagDaemon Software Research & Development**.

Built for **dynamic embedded computing**, **JIT hot-swapping inside running hosts (e.g. JUCE desktop apps)**, **bare-metal AOT compilation ("Hard Iron")**, and **beyond-bleeding-edge programming abstractions**.

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
6. **[Datatypes, Standard Library & Packaging](06-Datatypes-Std-and-Packaging.md)**
   * Sum & Product types, numeric primitives, Monads (`Option`, `Result`).
   * Dual Compilation (JIT + AOT Bare-Metal "Hard Iron" `.exe` emit).
   * Standard library (`std::libc`) & Package Manager (`frate`).
7. **[Project Frust Specification](07-Project-Frust-Language.md)**
   * Master architecture summary for Frust (**F**unctional **Rust**).
8. **[Built-in Smart Pointers & Memory Model](08-Built-in-Smart-Pointers.md)**
   * Eliminating lifetime annotation friction (`'a`).
   * Core pointer primitives: `own T`, `shared T`, `weak T`, `raw* T`, `&T`.
   * OS Kernel & bare-metal MMIO register access.
9. **[Hardware Math Intrinsics & Compile-Time Tables](09-Math-Intrinsics-and-Tables.md)**
   * Hardware SIMD register primitives (`v128f`, `v256f`).
   * `build_time` compile-time lookup tables (LUTs) for bare-metal OS/DSP math.
   * Vectors, Matrices (`Mat4x4`), Complex Numbers (`Complex<T>`).
10. **[Monads & Composable Architecture](10-Monads-and-Effects.md)**
    * First-class monadic types (`Option<T>`, `Result<T, E>`, `State<S, A>`).
    * `do` notation syntax (`<-`) & `?` try propagation operator.
    * Generic `Monad<M, A>` interface contract & zero-cost method chaining.
11. **[Streams, Generators & Set-Builders](11-Streams-Generators-and-Set-Builders.md)**
    * Mathematical Set-Builder notation (`{ expr | pattern <- seq, guard }`).
    * Lazy stream generators (`generator { yield val }`).
    * Infinite Fibonacci and real-time audio signal streams.
12. **[Advanced Bleeding-Edge Features](12-Advanced-Bleeding-Edge-Features.md)**
    * Lisp-style AST Quotation (`quote` / `unquote`).
    * Refinement Types (`f32[-1.0 .. 1.0]`, `i32[!= 0]`).
    * Algebraic Effects & Resumable Handlers (`effect`, `perform`, `handle`, `resume`).
    * AST Content Hashing for 0ms OrcJIT Hot-Reloading.
13. **[Dimensional Units & Custom Operators](13-Dimensional-Units-and-Custom-Operators.md)**
    * Compile-time physical dimensions & SI unit safety (`Meter`, `Second`, `Joule`, `Newton`, `MPH`, `Hz`).
    * Zero-overhead runtime compilation to raw primitives (`f32`/`f64`).
    * Programmable custom infix, prefix, and postfix operators (`infix operator`, `|>`, `~>`, `deg`).
14. **[Interactive REPL & Dynamic JIT Driver](14-Interactive-REPL-Driver.md)**
    * Real-time in-memory LLVM OrcJIT evaluation shell (`frust i`).
    * Expression evaluation with unit & refinement inspection (`:type`, `:ir`, `:load`).
    * JUCE Desktop host integration.
15. **[Package Manager, Storage & Registry (Frate)](15-Package-Manager-and-Registry.md)**
    * Cargo-style SemVer dependency reconciliation & `frate.lock`.
    * Output targets: `bin` (Hard-Iron AOT), `lib` (Source/binary modules), `bundle` (`.frate`).
    * Server Registry Ecosystem: PostgreSQL metadata indexing & S3 archive storage (`https://lagdaemon.com/api/frate/`).
16. **[Imperative Classes & Objects](16-Imperative-Classes-and-Objects.md)**
    * Encapsulated fields, constructors (`new`), & instance methods (`&self`, `&mut self`).
    * Stack, unique heap (`own`), & ref-counted (`shared`) object allocation modes.
    * Explicit mutable variables (`mut`), loops (`while`, `for`), & procedural control flow.

---

*Repository*: [github.com/wwestlake/lagdaemon-tech-research](https://github.com/wwestlake/lagdaemon-tech-research)
*License*: MIT License

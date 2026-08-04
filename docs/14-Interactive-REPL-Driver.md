# 14 - Interactive REPL & Dynamic JIT Driver

The **Frust Interactive REPL** (`frust i` / `frust-repl`) is a high-performance shell and embedded evaluation driver built directly on **LLVM OrcJIT v2**.

---

## 1. REPL Execution Workflow

Unlike slow bytecode interpreters, the Frust REPL is a real-time **LLVM OrcJIT shell**. Code typed into the prompt is compiled into native machine code in memory within microseconds.

```
[ Code Input ] ──> [ AST & Unit Check ] ──> [ LLVM IR ] ──> [ OrcJIT In-Memory Execution ]
```

---

## 2. Interactive Session Example

```frust
$ frust i
frust> let dist = 100.0 * Meter
val dist: f32[Meter] = 100.0

frust> let time = 9.58 * Second
val time: f32[Second] = 9.58

frust> dist / time
val it: f32[Meter / Second] = 10.4384

frust> :type dist / time
Type: f32[Meter / Second]
Dimensions: Length^1 * Time^-1
```

---

## 3. Built-in Commands

* `:type <expr>`: Displays type, refinement bounds, and unit dimensions.
* `:ir <expr>`: Prints generated LLVM IR code.
* `:ast <expr>`: Displays Abstract Syntax Tree representation.
* `:load <file.frust>`: Dynamically loads and JIT-compiles a module into memory.
* `:reset`: Clears current OrcJIT `JITDylib` scope.

# LagDaemon Software Research & Development Roadmap

## Project Vision & Architecture
Building a modern custom programming language environment centered around **dynamic REPL execution**, **C/C++ interop**, **WebAssembly targeting**, and **JUCE desktop application integration**.

---

## Key Technical Requirements

### 1. Execution Engine: OrcJIT (In-Memory / REPL)
* **No Heavy Standalone AOT Linking required**: Code is generated directly into memory using LLVM's `OrcJIT` (ORCv2 API).
* **Interactive REPL**: Expressions and functions are dynamically compiled to native memory pages and invoked instantly.
* **Hot-reloading & Dynamic Symbol Lookup**: Functions can be redefined on the fly during development.

### 2. Targets: WebAssembly + Host Native
* `X86_64` / `AArch64` for host execution and REPL JIT.
* `WebAssembly` (`wasm32` / `wasm64`) target enabled for outputting WebAssembly binaries from the custom language frontend.

### 3. C Interop & Header Parsing
* Integrated `Clang` / `libclang` frontend support to parse C headers on demand.
* Enables calling external C libraries, OS APIs, and C bridges into JUCE components directly from language scripts.

### 4. Desktop Host Environment (JUCE)
* JUCE (C++) UI for IDE tools, audio/visual node graphs, script editors, and interactive REPL consoles.
* Built with CMake, linking against our custom LLVM build (`LLVM_ENABLE_RTTI=ON` enabled for C++ compatibility).

---

## Verified LLVM Package Locations

| Platform | Location / Details | Status |
| :--- | :--- | :--- |
| **Windows x64** | `D:\vcpkg\packages\llvm_x64-windows` (LLVM 18.1.6) | **VERIFIED LOCAL** |
| **Linux (WSL Ubuntu)** | [build_llvm_wsl.sh](file:///d:/000%20Tech%20Research/build_llvm_wsl.sh) | **READY TO BUILD** |

---

## Immediate Next Steps (Post-Reset)
1. Run `./build_llvm_wsl.sh` inside WSL Ubuntu to build the Linux trimmed LLVM package.
2. Initialize project CMake structure connecting LLVM OrcJIT with JUCE on Windows using `D:\vcpkg\packages\llvm_x64-windows`.
3. Build prototype Lexer/Parser and basic AST -> LLVM IRBuilder stage.
4. Launch initial REPL loop!

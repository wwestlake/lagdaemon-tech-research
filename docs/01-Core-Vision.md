# 01 - Core Vision & Philosophy

## 1. Embedded Dynamic Computing Engine
The core purpose of the LagDaemon language suite is **embedded dynamic code creation**. Instead of compiling code out-of-band and restarting host applications, the language compiler is designed to be embedded directly inside a running host application (such as JUCE audio/visual apps, game engines, or OS runtimes).

> *"All programming is really dynamic code creation while another app is running—it's just that usually, the host program is the Operating System."*

---

## 2. Key Strategic Goals
1. **Dynamic Hot-Reloading via LLVM OrcJIT**: Modules and components can be edited, recompiled into memory, and swapped at runtime without stopping the host application or invalidating host memory buffers.
2. **First-Class Verifiable Components**: Components are first-class values exposing verifiable input/output contracts.
3. **No Esoteric Templates**: Metaprogramming is plain code that runs at compile time via OrcJIT. Types are first-class values (`Type`).
4. **Rust-Like Module Hierarchy**: File-system mapping for modular code organization, explicit visibility (`pub`), and structured namespace imports (`use`).
5. **Zero-Overhead Host Interop**: Native C/C++ ABI interop allowing host applications (e.g. JUCE) to invoke JIT-compiled functions with direct pointer access and zero marshalling tax.

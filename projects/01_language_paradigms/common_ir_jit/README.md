# Shared LLVM OrcJIT & IR Builder Core

## Focus
Common infrastructure for linking all paradigm modules against LLVM OrcJIT, providing unified symbol tables, C ABI function resolution, WebAssembly target configuration, and host memory execution.

## Shared Components
* **LLVM Context & Module Manager**: Manages active LLVM IR modules.
* **OrcJIT Engine Driver**: Instantiates ORCv2 JIT stack and resolves symbols.
* **Target Machine Configurator**: Switches between native host codegen and WebAssembly emit.

# Shared CEL

This module will host the shared CEL language infrastructure used across
the Creation Suite.

Shared here:

- frontend/parser/sema
- runtime and JIT core
- node bridge/code generation core
- common host ABI rules
- shared build and LLVM integration

Left per app:

- app-specific domain intrinsics
- domain policy
- any capability gating that prevents code from running in the wrong app

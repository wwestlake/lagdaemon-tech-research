#pragma once

#include <vector>

#include "intrinsic_trampolines.h" // shared/CEL: AbiSymbol

namespace ce::lang::jit {

// Creation Engine's own World-domain host-ABI trampolines (World/Entity/
// Transform, see apps/CreationEngine/Language/include/lang/intrinsics.def's
// World section) -- shared/CEL knows nothing about these; they're merged
// in as `extraSymbols`/`extraDomains` to RegisterAbiTrampolines wherever
// Engine JITs a module (world_runtime.cpp, script_runtime.cpp).
std::vector<AbiSymbol> GetWorldAbiTrampolines();

} // namespace ce::lang::jit

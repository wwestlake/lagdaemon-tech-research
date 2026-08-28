#pragma once

#include <string>
#include <vector>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

// Foley's own graph -> CEL textual source generation - separate from GenerateSource
// (graph_to_source.h), same reasoning as foley_node_catalog.h: share the codegen APPROACH (emit
// real .cel source text and run it through CEL's actual parse/sema/JIT pipeline, never a bespoke
// interpreter - see graph_to_source.h's own architecture note, which applies equally here), but
// never merge the domain-specific dispatch tables. Foley's node types (PlaySample, GainMix,
// RandomSelect, Delay, ...) have nothing in common with CreationEngine's ECS node types
// (SetPosition, Spawn, GetVariable, ...) that a shared EmitNode dispatch could meaningfully cover.
//
// Scoped down from graph_to_source.h's full feature set for this first pass: no lineToNode source
// map / CheckGeneratedSource diagnostic-to-node mapping (nothing consumes it yet - no Foley panel
// UI exists to show inline diagnostics against), and at most one OnTrigger entry node per graph
// (multiple independently-named triggers is a natural follow-up once something actually needs to
// invoke a specific trigger by name at runtime).

namespace ce::lang::nodegen::foley {

struct FoleyGraphToSourceResult {
    bool ok = false;
    std::string source;
    std::vector<std::string> errors;
};

// Validates `graph` against `registry` (ValidateGraph - exec/data cycles, registry conformance)
// and, if that passes, walks the graph's single OnTrigger entry node's exec chain to emit
// `func on_trigger() { ... }`. A graph with no OnTrigger node, more than one, or any exec-chain
// error (ambiguous wiring, missing pins) is a generation error (not a crash) - see `errors`.
FoleyGraphToSourceResult GenerateFoleySource(const ce::node_system::Graph& graph,
                                             const ce::node_system::NodeTypeRegistry& registry);

} // namespace ce::lang::nodegen::foley

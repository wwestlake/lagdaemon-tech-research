#pragma once

#include <string>
#include <vector>

#include "lang/diagnostics.h"
#include "lang/source_location.h"
#include "node_system/graph.h"
#include "node_system/type_registry.h"

// GS9: graph -> CEL textual source generation. Deliberately produces a
// std::string of real .cel source text rather than constructing an AST
// (ce::lang::Program) directly, per docs/GS_SCRIPTING_PLAN.md's own
// architecture decision: the graph and the text language must be
// provably one language, sharing the identical parse/sema/IR-gen/JIT
// pipeline, not two AST-construction code paths that could drift apart.
// The generated text is meant to be readable and hand-editable (a
// future "View Generated Code" pane), not just internally consumed.
//
// This module (ce_lang_nodegen) depends on ce_lang_frontend and
// node_system only -- explicitly NOT ce_lang_jit/LLVM (see
// Language/CMakeLists.txt's own comment on the target chain). Actually
// running the generated text is celc's job (--run-graph), by handing
// the string to ce_lang_jit exactly as it would any other .cel file.
// GS11's CheckGeneratedSource (below) uses ce_lang_frontend's own
// ParseProgram/AnalyzeProgram directly for the same LLVM-free reason --
// diagnostics don't need a JIT to exist.

namespace ce::lang::nodegen {

// GS11: `trace` inserts a real, runtime `log_int("trace node ", <id>)`
// call as the first thing every exec-chain node does when actually
// reached -- CAPABILITIES section 8's "traceable rule/event execution
// logging," implemented purely as a codegen-time text transform (no new
// ABI/intrinsic needed -- log_int already exists). Because it's a real
// call, not a static annotation, a branch/loop iteration not taken at
// runtime produces no trace line for it -- the trace reflects what
// actually ran, not what's merely present in the graph.
struct GenerateOptions {
    bool trace = false;
};

struct GraphToSourceResult {
    bool ok = false;
    std::string source;
    std::vector<std::string> errors;

    // GS11's source map: lineToNode[N] (1-based, matching
    // ce::lang::SourceLocation::line) is the NodeId whose emitted block
    // that line belongs to, or 0 if unowned (e.g. a file-scope `var`
    // hoisted line, or line 0/out of range). Built by scanning the
    // generated text's own `// @node <id>` comments -- see
    // graph_to_source.cpp's BuildLineToNodeMap.
    std::vector<ce::node_system::NodeId> lineToNode;
};

// Validates `graph` against `registry` (ValidateGraph -- exec/data
// cycles, registry conformance) and, if that passes, walks each
// OnStart/OnTick/SubgraphEntry entry node's exec chain to emit one
// top-level function per entry: `func on_start(self: entity)` /
// `func on_tick(self: entity, dt: float)` for the fixed lifecycle hooks
// (the same lifecycle CelScriptRuntime/Simulation::Step require -- see
// docs/SCRIPTING_ABI.md), or `func <name>()` for each GS11 SubgraphEntry
// node (name from its own "name" config pin -- see node_catalog.h).
// Each emitted statement/block is preceded by a `// @node <id>` comment,
// which also backs `lineToNode`.
//
// A graph with none of {OnStart, OnTick, SubgraphEntry}, more than one
// OnStart/OnTick, duplicate/invalid SubgraphEntry names, or a
// CallSubgraph referencing a name no SubgraphEntry in THIS graph
// defines, is a codegen error (not a crash) -- see `errors`. The last
// case is GS11's whole point: unlike CallFunction (which trusts an
// externally-defined target and lets sema catch a wrong name -- see
// CheckGeneratedSource below for how THAT diagnostic maps back to a
// node), CallSubgraph's target is validated at generation time because
// it's supposed to be self-contained within the graph.
GraphToSourceResult GenerateSource(const ce::node_system::Graph& graph, const ce::node_system::NodeTypeRegistry& registry,
                                    GenerateOptions options = {});

// GS11: one parse/sema diagnostic from compiling `genResult.source`,
// with `nodeId` resolved through `genResult.lineToNode` (0 if the
// diagnostic's line doesn't map to any node -- e.g. a file-scope `var`
// declaration line). This is what lets a UI (LogicPanel) or a headless
// check (`celc --check-graph-diagnostics`) say "node 7 is broken"
// instead of surfacing a raw compiler error with no graph-relative
// meaning.
struct NodeDiagnostic {
    ce::node_system::NodeId nodeId = 0;
    ce::lang::DiagCode code = ce::lang::DiagCode::SyntaxError;
    ce::lang::SourceLocation loc;
    std::string message;
};

struct CheckSourceResult {
    bool ok = true;
    std::vector<NodeDiagnostic> diagnostics;
};

// Parses and semantically analyzes `genResult.source` (the SAME
// ce_lang_frontend calls celc's own --check makes -- no JIT, no ABI,
// nothing LLVM) and maps every resulting diagnostic back to a node via
// `genResult.lineToNode`. Exists because generation-time validation
// (GenerateSource's own `errors`) only catches what codegen itself can
// see (missing entry nodes, an unresolvable CallSubgraph target, ...);
// anything sema alone catches -- e.g. a CallFunction node naming a
// function that doesn't actually exist anywhere in the program --
// still needs this second pass to attribute the failure to a node
// rather than just a source line nobody authoring the GRAPH ever saw.
CheckSourceResult CheckGeneratedSource(const GraphToSourceResult& genResult);

} // namespace ce::lang::nodegen

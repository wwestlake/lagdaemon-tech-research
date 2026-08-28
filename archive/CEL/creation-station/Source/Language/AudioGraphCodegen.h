#pragma once

#include <string>
#include <vector>

#include "node_system/graph.h"
#include "node_system/type_registry.h"

// Signal Lab's own graph -> CEL text generator, deliberately separate from
// shared/CEL/src/nodegen/graph_to_source.cpp (the Core/Event control-graph
// codegen). Each Signal Lab node type is a high-level, domain-specific
// algorithm (a "Sine Oscillator" node doesn't correspond to one CEL
// expression the way a Core "Add" node does) -- so this file's job is to
// hold the "how this node type is actually built in CEL" recipe per type,
// not walk a generic exec chain the way the control-graph codegen does.
//
// v1 scope: proves the graph -> CEL -> JIT -> real numeric result pipeline
// on the simplest possible case using ONLY CEL's already-existing `sin`
// Core intrinsic -- no new native ABI/intrinsic registration needed yet.
// A real buffer-filling render (N samples, host-provided buffer) needs its
// own AudioScriptContext + Audio-domain intrinsics.def extension, the same
// pattern Creation Engine used for its World domain -- that is the next
// slice after this one proves out, not attempted here. See
// docs/Signal-Lab-Node-Graph-Spec.md.

namespace cw::audionodes
{

struct GraphToSourceResult
{
    bool ok = false;
    std::string source;
    std::vector<std::string> errors;
};

// Builds the trivial two-node demo graph the vertical slice proves the
// pipeline on: one SineOscillator (level = levelValue) connected to one
// Output. Node ids/positions are arbitrary -- this graph is never saved,
// only compiled.
ce::node_system::Graph BuildSineToOutputDemoGraph(const ce::node_system::NodeTypeRegistry& registry,
                                                   float levelValue);

// Validates `graph` against `registry` and, if that passes, walks back
// from the Output node's connected signal source and emits a single CEL
// function computing one sample: `func compute_sample() -> float { return
// sin(<fixed phase>) * <level>; }`. `level` comes from the SineOscillator
// node's `level` input pin's literal value (v1: unconnected/literal only --
// a level fed by another node's output is a later slice, once parameter
// ports actually drive codegen instead of just existing visually).
GraphToSourceResult GenerateAudioSource(const ce::node_system::Graph& graph,
                                         const ce::node_system::NodeTypeRegistry& registry);

} // namespace cw::audionodes

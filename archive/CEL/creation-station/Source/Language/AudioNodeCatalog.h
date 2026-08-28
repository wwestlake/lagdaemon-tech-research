#pragma once

#include "node_system/type_registry.h"

// Signal Lab's own domain-level node catalog -- deliberately separate from
// shared/CEL's Core/Event control-graph catalog (shared/CEL/include/lang/
// nodegen/node_catalog.h). That catalog models control logic (OnStart/
// OnTick/Branch, exec-chain graphs) -- the "control surface" every app
// uses. Signal Lab is a different kind of graph: a domain-level signal-flow
// process (continuous dataflow, no execution steps), so it gets its own
// catalog and its own codegen (see AudioGraphCodegen.h), even though it
// reuses the same underlying node_system::Graph/Node/Pin/NodeTypeRegistry
// data structures (Domain::Audio and DataType::AudioSignal already exist
// there for exactly this purpose).
//
// This is v1 scope: just enough node types (SineOscillator, Output) to
// prove the graph -> CEL -> JIT -> JUCE DSP pipeline end to end on the
// simplest possible case. See docs/Signal-Lab-Node-Graph-Spec.md for the
// full roadmap this is the first slice of.

namespace cw::audionodes
{

namespace NodeType
{
inline constexpr const char* SineOscillator = "SineOscillator";
inline constexpr const char* Output = "Output";
} // namespace NodeType

namespace PinName
{
inline constexpr const char* SignalIn = "signalIn";
inline constexpr const char* SignalOut = "signalOut";
inline constexpr const char* Level = "level";
} // namespace PinName

// Builds a fresh registry containing every v1 Signal Lab node type,
// tagged ce::node_system::Domain::Audio. Returned by value, same reasoning
// as shared/CEL's BuildCoreNodeCatalog: registrations are cheap, callers
// (codegen, the UI, tests) shouldn't have to share one global instance.
ce::node_system::NodeTypeRegistry BuildAudioNodeCatalog();

} // namespace cw::audionodes

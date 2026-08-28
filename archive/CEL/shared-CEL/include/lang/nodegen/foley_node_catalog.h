#pragma once

#include "node_system/type_registry.h"

// Foley's own node catalog - deliberately separate from BuildCoreNodeCatalog (node_catalog.h),
// per the suite's explicit consolidation stance: share the generic node-graph editing machinery
// (ce::node_system::Graph/NodeTypeRegistry, and the ported NodeGraphComponent/NodeInspector/
// NodePalette UI in shared/NodeEditorUI) across every domain, but never merge domain-specific
// node catalogs. Signal Lab's audio-DSP nodes, CreationEngine's ECS/game nodes, and Foley's own
// sequencing nodes are three separate catalogs on the same shared foundation, not one shared
// catalog.
//
// Foley is picture-sync sound design: placing/performing sounds in sync with a video, sequenced
// with choice logic (branch, random pick, delay) - this catalog models exactly that, not general
// game/ECS logic. An entry point is `OnTrigger` (an event/cue firing - analogous to OnStart, but
// named for what it actually means in this domain) rather than OnStart/OnTick, since Foley graphs
// don't have a per-frame tick concept the way CreationEngine's ECS nodes do.

namespace ce::lang::nodegen::foley {

namespace NodeType {
inline constexpr const char* OnTrigger = "OnTrigger";
inline constexpr const char* PlaySample = "PlaySample";
inline constexpr const char* GainMix = "GainMix";
inline constexpr const char* Sequence = "Sequence";
inline constexpr const char* Branch = "Branch";
inline constexpr const char* RandomSelect = "RandomSelect";
inline constexpr const char* Delay = "Delay";
} // namespace NodeType

namespace PinName {
inline constexpr const char* ExecIn = "execIn";
inline constexpr const char* ExecOut = "execOut";
inline constexpr const char* ExecOut0 = "execOut0";
inline constexpr const char* ExecOut1 = "execOut1";
inline constexpr const char* ExecOut2 = "execOut2";
inline constexpr const char* ExecTrue = "execTrue";
inline constexpr const char* ExecFalse = "execFalse";
inline constexpr const char* ExecA = "execA";
inline constexpr const char* ExecB = "execB";
inline constexpr const char* Condition = "condition";
inline constexpr const char* SampleName = "sampleName";
inline constexpr const char* Gain = "gain";
inline constexpr const char* Seconds = "seconds";
inline constexpr const char* Name = "name";
} // namespace PinName

// Builds a fresh registry containing every Foley catalog type. Returned by value, same reasoning
// as BuildCoreNodeCatalog: cheap to construct, keeps callers (the Foley panel, codegen, tests)
// from sharing ownership of one global instance.
ce::node_system::NodeTypeRegistry BuildFoleyNodeCatalog();

} // namespace ce::lang::nodegen::foley

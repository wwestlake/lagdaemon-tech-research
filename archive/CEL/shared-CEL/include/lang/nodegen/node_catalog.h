#pragma once

#include "node_system/type_registry.h"

// GS9: the v1 Core/Event node catalog -- the concrete set of registered
// node TYPES a graph can be built from, backing the "one editor, one
// language" promise (docs/GS_SCRIPTING_PLAN.md) with real pin shapes
// instead of GS8's free-form AddNode. Every type here has a fixed,
// registry-enforced pin signature (see NodeTypeRegistry); per-instance
// "configuration" that isn't naturally a wired value (a Compare node's
// operator, a variable's name, a called function's name) is modeled as
// an intentionally-unconnectable Data pin carrying a literal default --
// see graph_to_source.h's own comment for why, and NodeType/PinName
// below for the exact vocabulary graph_to_source.cpp dispatches on.
//
// Deliberate v1 scope limits (documented here so they don't read as
// oversights later):
// - Compare/arithmetic operate on Float only (no Vec3/Int variants yet).
// - Log only takes a literal string message (no log_int/log_float/
//   log_vec3 node variants) -- CEL's String type is itself literal-only
//   (see intrinsics.def), so this mirrors a real language constraint,
//   not an arbitrary one.
// - GetVariable/SetVariable model exactly one implicit variable *type*
//   (Float), named via an unconnected String "name" pin. A real
//   per-type variable node set is a natural, additive follow-up.
// - CallFunction supports only a zero-argument, void target function,
//   referenced by an unconnected String "function" name pin -- it emits
//   a plain call statement and trusts sema to catch a wrong/missing
//   name. GS11's CallSubgraph (below) is the validated counterpart:
//   generation itself checks the target actually exists as a
//   SubgraphEntry in the SAME graph, rather than trusting sema.
// - SubgraphEntry/CallSubgraph (GS11): a reusable sub-graph is also
//   zero-argument/void, same as CallFunction -- a SubgraphEntry has no
//   `self`/`dt` output, so its body cannot reference either. Typed
//   sub-graph parameters are a natural, additive follow-up, not built
//   here.
// - Sequence has a fixed arity of 3 ordered exec outputs
//   (execOut0/execOut1/execOut2); a variable-arity Sequence needs an
//   editor affordance to add/remove pins, which belongs to GS10.

namespace ce::lang::nodegen {

// String constants for every registered typeName and every well-known
// pin name the catalog uses -- shared between BuildCoreNodeCatalog's
// own registration calls and graph_to_source.cpp's per-type dispatch,
// so the two can never drift out of sync via a typo in a string
// literal.
namespace NodeType {
inline constexpr const char* OnStart = "OnStart";
inline constexpr const char* OnTick = "OnTick";
inline constexpr const char* Sequence = "Sequence";
inline constexpr const char* Branch = "Branch";
inline constexpr const char* While = "While";
inline constexpr const char* CompareLt = "CompareLt";
inline constexpr const char* CompareLe = "CompareLe";
inline constexpr const char* CompareGt = "CompareGt";
inline constexpr const char* CompareGe = "CompareGe";
inline constexpr const char* CompareEq = "CompareEq";
inline constexpr const char* CompareNeq = "CompareNeq";
inline constexpr const char* Add = "Add";
inline constexpr const char* Sub = "Sub";
inline constexpr const char* Mul = "Mul";
inline constexpr const char* Div = "Div";
inline constexpr const char* MakeVec3 = "MakeVec3";
inline constexpr const char* GetPosition = "GetPosition";
inline constexpr const char* SetPosition = "SetPosition";
inline constexpr const char* Spawn = "Spawn";
inline constexpr const char* Destroy = "Destroy";
inline constexpr const char* Log = "Log";
inline constexpr const char* GetVariable = "GetVariable";
inline constexpr const char* SetVariable = "SetVariable";
inline constexpr const char* CallFunction = "CallFunction";
inline constexpr const char* SubgraphEntry = "SubgraphEntry";
inline constexpr const char* CallSubgraph = "CallSubgraph";
} // namespace NodeType

namespace PinName {
inline constexpr const char* ExecIn = "execIn";
inline constexpr const char* ExecOut = "execOut";
inline constexpr const char* ExecOut0 = "execOut0";
inline constexpr const char* ExecOut1 = "execOut1";
inline constexpr const char* ExecOut2 = "execOut2";
inline constexpr const char* ExecTrue = "execTrue";
inline constexpr const char* ExecFalse = "execFalse";
inline constexpr const char* LoopBody = "loopBody";
inline constexpr const char* Completed = "completed";
inline constexpr const char* Self = "self";
inline constexpr const char* Dt = "dt";
inline constexpr const char* Condition = "condition";
inline constexpr const char* A = "a";
inline constexpr const char* B = "b";
inline constexpr const char* Result = "result";
inline constexpr const char* X = "x";
inline constexpr const char* Y = "y";
inline constexpr const char* Z = "z";
inline constexpr const char* Value = "value";
inline constexpr const char* Entity = "entity";
inline constexpr const char* Position = "position";
inline constexpr const char* Message = "message";
inline constexpr const char* Name = "name";
inline constexpr const char* Function = "function";
} // namespace PinName

// Builds a fresh registry containing every v1 catalog type. Returned
// by value (registrations are cheap, and this keeps callers -- codegen,
// tests, and eventually GS10's node palette -- from having to share
// ownership of one global instance).
ce::node_system::NodeTypeRegistry BuildCoreNodeCatalog();

} // namespace ce::lang::nodegen

#pragma once

namespace ce::lang::tools {

// GS8's own verification vehicle (`celc --selftest-graph`): builds a
// graph programmatically via a small test-only NodeTypeRegistry,
// round-trips it through SerializeGraph/DeserializeGraph and asserts
// structural equality, and separately asserts that DetectExecCycle and
// Graph::Connect's incompatible-type rejection both fire correctly.
// Prints PASS/FAIL and returns 0/1, matching --selftest-jit's
// (GS1) convention.
int RunSelfTestGraph();

} // namespace ce::lang::tools

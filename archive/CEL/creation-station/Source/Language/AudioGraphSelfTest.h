#pragma once

#include <string>

// Signal Lab's first vertical-slice proof: node graph -> generated CEL
// text -> parsed/analyzed -> JIT-compiled -> executed -> real numeric
// result, using Station's own Domain::Audio node catalog (AudioNodeCatalog.h)
// and codegen (AudioGraphCodegen.h). This is Station's first step toward an
// Engine-`world_runtime.h`-style domain runtime -- see
// docs/Signal-Lab-Node-Graph-Spec.md's "Compilation & cross-suite execution
// model" section for the full roadmap this is the first slice of.
//
// Deliberately minimal: proves the pipeline on ONE computed sample via
// CEL's already-existing `sin` Core intrinsic, not a real N-sample buffer
// render yet (that needs a new AudioScriptContext + Audio-domain
// intrinsics.def extension, the same pattern Engine used for World -- next
// slice, not this one).

namespace cw::audionodes
{

struct SelfTestResult
{
    bool ok = false;
    std::string message;
    float computedSample = 0.0f;
    float expectedSample = 0.0f;
};

// Builds the Sine->Output demo graph, generates CEL source from it,
// compiles and runs it via the real JIT, and compares the result against
// the same computation done natively in C++ (std::sin(phase) * level) --
// the acceptance bar from the plan's vertical slice.
SelfTestResult RunAudioGraphSelfTest();

} // namespace cw::audionodes

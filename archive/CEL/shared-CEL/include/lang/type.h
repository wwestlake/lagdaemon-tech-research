#pragma once

#include <string>
#include <vector>

namespace ce::lang {

// CEL v1's deliberately closed type system -- every one of these maps to
// exactly one LLVM type with no lowering ambiguity (see GS4). String is
// NOT a first-class value type: it exists only as a literal, usable
// solely as a direct argument to an intrinsic call (enforced in
// sema.cpp) -- there is no way to declare a variable of type String, and
// no operator accepts it. Unknown is an error-recovery sentinel: sema
// assigns it after a type error so downstream checks on the same
// expression don't cascade into a wall of duplicate diagnostics.
enum class Type { Void, Int, Float, Bool, Vec2, Vec3, Vec4, Mat2, Mat3, Mat4, Entity, String, Unknown };

const char* ToString(Type type);

// Resolves a type name as written in source (after `:` or `->`) to a
// Type -- Type::Unknown if it isn't one of the known names. String is
// deliberately not resolvable here: it can only ever appear as a
// literal's inferred type, never as a declared one.
Type ParseTypeName(const std::string& name);

// GS-Interop: which capability group an intrinsic belongs to -- the
// mechanism behind cross-app intrinsic gating (see
// docs/CROSS_APP_LANGUAGE_DOMAINS.md). Every intrinsic in
// intrinsics.def is tagged with exactly one of these; a host (celc, or
// eventually another app's CelScriptRuntime construction) supplies an
// IntrinsicDomainSet of what it allows, and both sema (compile-time,
// see sema.cpp's CheckCall) and JIT symbol registration (see
// abi_registration.cpp) reject anything outside it.
//
// Only two values exist because only two are backed by real,
// implemented intrinsics today -- Core (sqrt/vec3/log/... -- no World
// access at all) and World (spawn/get_position/... -- everything that
// touches ce::engine::World through intrinsic_trampolines.cpp). The
// wider cross-app vocabulary (physics/audio/patch/tracker/mixer/
// performance/timeline/render/scene/broadcast/live/movie/instrument)
// is real -- it's what Creation Station/Movie/Live's own
// AppLanguagePolicy layers already declare -- but none of it is
// implemented CEL intrinsic surface yet, so it isn't enumerated here.
// Adding a value here should happen exactly when a real intrinsic
// needs it, not speculatively ahead of one existing.
enum class IntrinsicDomain { Core, World };

const char* ToString(IntrinsicDomain domain);

// A small bitset over IntrinsicDomain -- what one host's "capability
// profile" (per the cross-app architecture note) actually is. All()
// is the default every existing call site uses today (Runtime::
// CompileAndRun/RunWorldProgram, CelScriptRuntime::Compile, celc's
// subcommands) -- "every intrinsic available," i.e. today's real,
// unchanged behavior. A host that wants to restrict itself constructs
// one explicitly (see celc's --domains flag).
class IntrinsicDomainSet {
public:
    static IntrinsicDomainSet All();
    // Takes a std::vector rather than std::initializer_list so both a
    // literal (`Only({IntrinsicDomain::Core})`, via vector's own
    // initializer_list constructor) and a runtime-built list (celc's
    // --domains CSV parsing) work through the same function.
    static IntrinsicDomainSet Only(const std::vector<IntrinsicDomain>& domains);

    bool Contains(IntrinsicDomain domain) const;

private:
    // Bit i set means IntrinsicDomain with underlying value i is
    // allowed. A plain unsigned bitmask is enough for the handful of
    // domains this enum will ever realistically hold (see the enum's
    // own comment on why new values are added deliberately, not
    // speculatively) -- no need for std::bitset's extra ceremony.
    unsigned mask_ = 0;
};

} // namespace ce::lang

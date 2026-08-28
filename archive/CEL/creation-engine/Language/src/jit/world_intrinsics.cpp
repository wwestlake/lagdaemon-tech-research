#include "lang/jit/world_intrinsics.h"

#include <cstdint>

#include <entt/entt.hpp>

#include "engine/core_components.h"
#include "engine/script_context.h"
#include "engine/world.h"

// Every function here is the real implementation behind a World-domain
// intrinsics.def cSymbol entry (World/Entity/Transform) -- extern "C",
// noexcept, called with World::RegistryMutex() already held by the
// caller (world_runtime.cpp's RunWorldProgram / the future
// Simulation::Step), per the ABI's rule 6: none of these lock it
// themselves. `ce::engine::ScriptContext*` is always the first
// parameter -- this converts implicitly from the
// `ce::lang::jit::ScriptContext*` the generated IR actually passes,
// since ce::engine::ScriptContext derives from it with no fields
// inserted before the base (see engine/script_context.h's own comment).
//
// vec3 arguments/returns cross this boundary as `float*` (3 packed
// floats, x/y/z) rather than by value, per the ABI's rule 1. String
// arguments cross as an explicit (const char*, int64_t length) pair.

using ce::engine::ScriptContext;
using ce::engine::Transform;
using ce::engine::Vec3;

namespace {

entt::entity ToEntity(uint64_t id) {
    return static_cast<entt::entity>(static_cast<entt::id_type>(id));
}

uint64_t FromEntity(entt::entity e) {
    return static_cast<uint64_t>(static_cast<entt::id_type>(e));
}

Vec3 LoadVec3(const float* xyz) {
    return Vec3{ xyz[0], xyz[1], xyz[2] };
}

void StoreVec3(float* out, const Vec3& v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

// get_position/get_rotation/get_scale read (and set_* write) a
// Transform that may not exist yet on a given entity -- rather than
// making "every entity always has a Transform" an invariant the whole
// engine must maintain just for scripting's sake, these lazily
// get-or-emplace a default-constructed one, matching how a freshly
// `spawn()`-ed (not `spawn_at`) entity has no meaningful transform
// until a script gives it one.
Transform& GetOrCreateTransform(ScriptContext* ctx, entt::entity e) {
    return ctx->world->Registry().get_or_emplace<Transform>(e);
}

// Mirrors GetOrCreateTransform: an entity has no meaningful color
// override until a script actually calls set_color, so get_color on an
// entity that's never been touched lazily emplaces the {1,1,1} identity
// default (see ce::engine::Tint's own doc comment) rather than requiring
// every entity to carry one up front.
ce::engine::Tint& GetOrCreateTint(ScriptContext* ctx, entt::entity e) {
    return ctx->world->Registry().get_or_emplace<ce::engine::Tint>(e);
}

} // namespace

extern "C" {

// --- World -------------------------------------------------------------

int64_t ce_world_tick(ScriptContext* ctx) noexcept {
    return static_cast<int64_t>(ctx->world->CurrentTick());
}

float ce_world_time(ScriptContext* ctx) noexcept {
    return ctx->elapsedTime;
}

// --- Entity --------------------------------------------------------------

uint64_t ce_spawn(ScriptContext* ctx) noexcept {
    entt::entity e = ctx->world->CreateEntity();
    ctx->world->Registry().emplace<Transform>(e);
    return FromEntity(e);
}

uint64_t ce_spawn_at(ScriptContext* ctx, const float* xyz) noexcept {
    entt::entity e = ctx->world->CreateEntity();
    ctx->world->Registry().emplace<Transform>(e, Transform{ LoadVec3(xyz), Vec3{}, Vec3{ 1.0f, 1.0f, 1.0f } });
    return FromEntity(e);
}

void ce_destroy(ScriptContext* ctx, uint64_t entity) noexcept {
    entt::entity e = ToEntity(entity);
    if (ctx->world->Registry().valid(e)) {
        ctx->world->DestroyEntity(e);
    }
}

int32_t ce_is_valid(ScriptContext* ctx, uint64_t entity) noexcept {
    return ctx->world->Registry().valid(ToEntity(entity)) ? 1 : 0;
}

int64_t ce_entity_count(ScriptContext* ctx) noexcept {
    // basic_registry::storage<entity_type>() (non-const) returns the
    // entity pool itself BY REFERENCE (not a pointer -- unlike every
    // component-type storage lookup), always valid, creating it if this
    // is a brand-new World that has never created an entity. size()
    // includes recycled/tombstoned slots; free_list() is exactly that
    // recyclable count, so the difference is the number of entities
    // genuinely alive right now -- the same arithmetic
    // basic_registry::valid() itself uses internally.
    auto& pool = ctx->world->Registry().storage<entt::entity>();
    return static_cast<int64_t>(pool.size() - pool.free_list());
}

uint64_t ce_find_by_name(ScriptContext*, const char*, int64_t) noexcept {
    // No Name component exists in EngineCore yet (ce::scene::Name is
    // JUCE-facing, Source/Scene/Components.h, unreachable from here) --
    // stubbed to "always not found" rather than blocking the rest of
    // GS5's ABI on a name-lookup system this milestone's verification
    // scripts don't need. A real implementation is a follow-up once
    // EngineCore grows a framework-agnostic Name component.
    return FromEntity(entt::null);
}

// --- Transform -------------------------------------------------------

void ce_get_position(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).position);
}

void ce_set_position(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).position = LoadVec3(xyz);
}

void ce_get_rotation(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).eulerRotationRadians);
}

void ce_set_rotation(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).eulerRotationRadians = LoadVec3(xyz);
}

void ce_get_scale(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTransform(ctx, ToEntity(entity)).scale);
}

void ce_set_scale(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTransform(ctx, ToEntity(entity)).scale = LoadVec3(xyz);
}

// --- Color -------------------------------------------------------------

void ce_get_color(ScriptContext* ctx, uint64_t entity, float* outXyz) noexcept {
    StoreVec3(outXyz, GetOrCreateTint(ctx, ToEntity(entity)).color);
}

void ce_set_color(ScriptContext* ctx, uint64_t entity, const float* xyz) noexcept {
    GetOrCreateTint(ctx, ToEntity(entity)).color = LoadVec3(xyz);
}

// ce_log/ce_log_int/ce_log_float/ce_log_vec3/ce_watchdog_tick are NOT
// declared here -- they need no World/EnTT access (pure printing and
// ScriptContext budget bookkeeping), so shared/CEL's own
// intrinsic_trampolines.cpp already provides them as Core-domain
// intrinsics available to every app. A second definition here would
// be a duplicate-symbol link error against that one.

} // extern "C"

namespace ce::lang::jit {

std::vector<AbiSymbol> GetWorldAbiTrampolines() {
    return {
        { "ce_world_tick", reinterpret_cast<void*>(&ce_world_tick) },
        { "ce_world_time", reinterpret_cast<void*>(&ce_world_time) },
        { "ce_spawn", reinterpret_cast<void*>(&ce_spawn) },
        { "ce_spawn_at", reinterpret_cast<void*>(&ce_spawn_at) },
        { "ce_destroy", reinterpret_cast<void*>(&ce_destroy) },
        { "ce_is_valid", reinterpret_cast<void*>(&ce_is_valid) },
        { "ce_entity_count", reinterpret_cast<void*>(&ce_entity_count) },
        { "ce_find_by_name", reinterpret_cast<void*>(&ce_find_by_name) },
        { "ce_get_position", reinterpret_cast<void*>(&ce_get_position) },
        { "ce_set_position", reinterpret_cast<void*>(&ce_set_position) },
        { "ce_get_rotation", reinterpret_cast<void*>(&ce_get_rotation) },
        { "ce_set_rotation", reinterpret_cast<void*>(&ce_set_rotation) },
        { "ce_get_scale", reinterpret_cast<void*>(&ce_get_scale) },
        { "ce_set_scale", reinterpret_cast<void*>(&ce_set_scale) },
        { "ce_get_color", reinterpret_cast<void*>(&ce_get_color) },
        { "ce_set_color", reinterpret_cast<void*>(&ce_set_color) },
    };
}

} // namespace ce::lang::jit

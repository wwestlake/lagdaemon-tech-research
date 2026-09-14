#pragma once

#include <glm/glm.hpp>
#include <memory>

// Forward-declared Jolt types so this header stays cheap to include -
// the actual Jolt headers only appear in PhysicsWorld.cpp.
namespace JPH {
class PhysicsSystem;
class TempAllocatorImpl;
class JobSystemThreadPool;
class Character;
class BroadPhaseLayerInterface;
class ObjectVsBroadPhaseLayerFilter;
class ObjectLayerPairFilter;
}

namespace Harmonia {

// Real physics via Jolt, replacing the hand-rolled "snap Y to
// GroundPlane::heightAt() every frame, no collision at all" placeholder
// PlayerController used before. Owns Jolt's world, a static terrain body
// built from the SAME height function the visible ground mesh uses (so
// what you see is exactly what you collide with), and a capsule
// character controller - real gravity, real ground contact, real
// capsule-vs-terrain collision, not a manual position hack.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    // Builds the static terrain collision body (samples GroundPlane's
    // heightAt() over the same world extent the visible mesh covers) and
    // spawns the player's capsule character at the given start position.
    void initialise(const glm::vec3& characterStartPos, bool useFlatFloor = false);

    // moveDir is a normalised (or zero) horizontal desired-movement
    // vector in world space - PhysicsWorld handles turning that into a
    // real velocity, gravity, ground snapping, and collision response.
    void update(float dt, const glm::vec3& moveDir, bool jump, bool run);

    glm::vec3 characterPosition() const;
    bool isGrounded() const;

    // Snaps the character straight back to a position (feet-level, same
    // convention as characterStartPos in initialise()) and zeroes its
    // velocity - used for the fall-into-the-void safety net, not for
    // normal gameplay movement.
    void teleportCharacter(const glm::vec3& feetPos);

private:
    JPH::PhysicsSystem* system_ = nullptr;
    JPH::TempAllocatorImpl* tempAllocator_ = nullptr;
    JPH::JobSystemThreadPool* jobSystem_ = nullptr;
    JPH::BroadPhaseLayerInterface* broadPhaseLayerInterface_ = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilter* objectVsBroadPhaseLayerFilter_ = nullptr;
    JPH::ObjectLayerPairFilter* objectLayerPairFilter_ = nullptr;

    // JPH::Ref is Jolt's own intrusive ref-counted pointer - kept as a
    // raw owning pointer here (deleted in the destructor) so this header
    // doesn't need to include Jolt's ref-counting template at all.
    JPH::Character* character_ = nullptr;

    // Capsule dimensions - single source of truth (previously duplicated
    // as raw literals in initialise()/characterPosition()/teleportCharacter()).
    static constexpr float kCapsuleRadius = 0.35f;
    static constexpr float kCapsuleHalfHeight = 0.7f;
};

}

#include "PhysicsWorld.h"
#include "Client/Engine/Rendering/GroundPlane.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/Character.h>

#include <thread>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <iostream>

namespace Harmonia {

namespace {

// Two broad-phase/object layers is the standard minimal Jolt setup for
// "static world + one moving thing" - the terrain never moves, the
// character does.
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        // Terrain (NON_MOVING) doesn't need to collide with itself; the
        // character (MOVING) collides with everything.
        if (a == Layers::NON_MOVING && b == Layers::NON_MOVING) return false;
        return true;
    }
};

constexpr float kHalfExtent = 500.0f; // matches GroundPlane's own visual mesh extent
constexpr int   kHeightSamples = 129; // Jolt wants (power-of-two)+1-ish grids; close enough here

}

PhysicsWorld::PhysicsWorld() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    tempAllocator_ = new JPH::TempAllocatorImpl(16 * 1024 * 1024);
    unsigned threads = std::thread::hardware_concurrency();
    jobSystem_ = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                               threads > 1 ? (int)threads - 1 : 1);

    broadPhaseLayerInterface_ = new BPLayerInterfaceImpl();
    objectVsBroadPhaseLayerFilter_ = new ObjectVsBroadPhaseLayerFilterImpl();
    objectLayerPairFilter_ = new ObjectLayerPairFilterImpl();

    system_ = new JPH::PhysicsSystem();
    system_->Init(1024, 0, 1024, 1024,
                  *static_cast<BPLayerInterfaceImpl*>(broadPhaseLayerInterface_),
                  *static_cast<ObjectVsBroadPhaseLayerFilterImpl*>(objectVsBroadPhaseLayerFilter_),
                  *static_cast<ObjectLayerPairFilterImpl*>(objectLayerPairFilter_));
}

PhysicsWorld::~PhysicsWorld() {
    if (character_) {
        character_->RemoveFromPhysicsSystem();
        character_->Release();
    }
    delete system_;
    delete jobSystem_;
    delete tempAllocator_;
    delete broadPhaseLayerInterface_;
    delete objectVsBroadPhaseLayerFilter_;
    delete objectLayerPairFilter_;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void PhysicsWorld::initialise(const glm::vec3& characterStartPos) {
    // Sample the SAME heightAt() the visible ground mesh is built from,
    // over the same world extent - what you see is exactly what you
    // stand on, no separately-authored collision geometry to keep in
    // sync by hand.
    std::vector<float> heights((size_t)kHeightSamples * kHeightSamples);
    float step = (kHalfExtent * 2.0f) / (float)(kHeightSamples - 1);
    for (int z = 0; z < kHeightSamples; ++z) {
        for (int x = 0; x < kHeightSamples; ++x) {
            float wx = -kHalfExtent + x * step;
            float wz = -kHalfExtent + z * step;
            heights[(size_t)z * kHeightSamples + x] = GroundPlane::heightAt(wx, wz);
        }
    }

    JPH::HeightFieldShapeSettings hfSettings(
        heights.data(),
        JPH::Vec3(-kHalfExtent, 0.0f, -kHalfExtent),
        JPH::Vec3(step, 1.0f, step),
        (JPH::uint32)kHeightSamples);
    JPH::ShapeSettings::ShapeResult hfResult = hfSettings.Create();
    JPH::BodyInterface& bodyInterface = system_->GetBodyInterface();
    if (hfResult.IsValid()) {
        JPH::BodyCreationSettings terrainSettings(hfResult.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                                    JPH::EMotionType::Static, Layers::NON_MOVING);
        JPH::BodyID terrainId = bodyInterface.CreateAndAddBody(terrainSettings, JPH::EActivation::DontActivate);
        (void)terrainId;
    } else {
        std::cerr << "Harmonia: failed to build terrain collision shape: " << hfResult.GetError() << "\n";
    }

    // A simple capsule character - real gravity, real ground detection,
    // real collision response against the terrain above, via Jolt's own
    // Character class (not a hand-rolled height snap).
    const float radius = 0.35f;
    const float halfHeight = 0.7f; // ~1.4m tall capsule cylinder + caps
    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(halfHeight, radius);

    JPH::CharacterSettings settings;
    settings.mShape = capsule;
    settings.mLayer = Layers::MOVING;
    settings.mGravityFactor = 1.0f;
    settings.mFriction = 0.6f;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    JPH::RVec3 startPos(characterStartPos.x, characterStartPos.y + halfHeight + radius + 0.1f, characterStartPos.z);
    character_ = new JPH::Character(&settings, startPos, JPH::Quat::sIdentity(), 0, system_);
    character_->AddToPhysicsSystem(JPH::EActivation::Activate);
}

void PhysicsWorld::update(float dt, const glm::vec3& moveDir, bool jump) {
    if (!character_ || !system_) return;

    const float walkSpeed = 5.0f;
    const float jumpSpeed = 6.0f;

    JPH::Vec3 currentVel = character_->GetLinearVelocity();
    JPH::Vec3 desiredHorizontal = JPH::Vec3(moveDir.x, 0.0f, moveDir.z) * walkSpeed;

    bool grounded = character_->GetGroundState() == JPH::Character::EGroundState::OnGround;
    float verticalVel = currentVel.GetY();
    if (grounded) {
        verticalVel = jump ? jumpSpeed : 0.0f;
    } else {
        verticalVel += system_->GetGravity().GetY() * dt; // free fall
    }

    character_->SetLinearVelocity(JPH::Vec3(desiredHorizontal.GetX(), verticalVel, desiredHorizontal.GetZ()));

    const int collisionSteps = 1;
    system_->Update(dt, collisionSteps, tempAllocator_, jobSystem_);
    character_->PostSimulation(0.05f); // max separation distance for ground detection
}

glm::vec3 PhysicsWorld::characterPosition() const {
    if (!character_) return glm::vec3(0.0f);
    JPH::RVec3 p = character_->GetPosition();
    // Character::GetPosition() is the CAPSULE CENTRE, not the feet -
    // shift down by half-height+radius so callers get a feet-on-the-
    // ground position, matching what GroundPlane::heightAt() and the
    // rendered character model both expect.
    return glm::vec3((float)p.GetX(), (float)p.GetY() - (0.7f + 0.35f), (float)p.GetZ());
}

bool PhysicsWorld::isGrounded() const {
    if (!character_) return false;
    return character_->GetGroundState() == JPH::Character::EGroundState::OnGround;
}

}

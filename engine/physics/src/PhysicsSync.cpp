// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/core/Profiling.h>
#include <interi/core/math/Transform.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/PhysicsSync.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
// clang-format on

#include <algorithm>
#include <cstdint>

namespace interi::physics
{

namespace
{

JPH::EMotionType JoltMotionType(MotionType motion) noexcept
{
    switch (motion)
    {
    case MotionType::Static:
        return JPH::EMotionType::Static;
    case MotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
        break;
    }
    return JPH::EMotionType::Dynamic;
}

JPH::BodyCreationSettings MakeSettings(const RigidBodyComponent& body, const ColliderComponent& collider,
                                       const Transform& transform, Entity entity)
{
    JPH::BodyCreationSettings settings(collider.shape, ToJolt(transform.position), ToJolt(transform.rotation),
                                       JoltMotionType(body.motion), ToJolt(body.layer));
    settings.mFriction = body.friction;
    settings.mRestitution = body.restitution;
    settings.mLinearDamping = body.linearDamping;
    settings.mAngularDamping = body.angularDamping;
    settings.mMotionQuality = body.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    settings.mUserData = entity.Packed();
    if (body.mass > 0.0f)
    {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = body.mass;
    }
    return settings;
}

/// Adds created bodies in one broadphase batch (much cheaper than one at a time for thousands of debris pieces).
void AddBatch(JPH::BodyInterface& bodies, std::vector<JPH::BodyID>& ids, JPH::EActivation activation)
{
    if (ids.empty())
    {
        return;
    }
    const auto count = static_cast<int>(ids.size());
    const JPH::BodyInterface::AddState state = bodies.AddBodiesPrepare(ids.data(), count);
    bodies.AddBodiesFinalize(ids.data(), count, state, activation);
}

} // namespace

PhysicsSync::PhysicsSync(PhysicsWorld& physics)
    : m_physics(&physics)
{
    m_toCreate.With<RigidBodyComponent, ColliderComponent, Transform>().Without<PhysicsBodyComponent>();
    m_withBody.With<RigidBodyComponent, PhysicsBodyComponent, Transform>();
}

PhysicsSync::~PhysicsSync()
{
    std::vector<JPH::BodyID> ids;
    ids.reserve(m_bodies.size());
    for (const auto& entry : m_bodies)
    {
        ids.push_back(entry.second);
    }
    if (!ids.empty())
    {
        JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
        bodies.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
        bodies.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
    }
}

void PhysicsSync::Step(World& world, JobSystem& jobs, float deltaSeconds)
{
    DestroyRemovedBodies(world);
    CreateBodies(world);
    PushKinematic(world, deltaSeconds);
    m_physics->Step(deltaSeconds);
    PullTransforms(world, jobs);
}

void PhysicsSync::DestroyRemovedBodies(World& world)
{
    INTERI_ZONE_NAMED("PhysicsSync::DestroyRemovedBodies");
    std::vector<JPH::BodyID> removed;
    std::erase_if(m_bodies,
                  [&](const std::pair<Entity, JPH::BodyID>& entry)
                  {
                      const Entity entity = entry.first;
                      if (world.IsAlive(entity) && world.Has<RigidBodyComponent>(entity))
                      {
                          return false;
                      }
                      removed.push_back(entry.second);
                      (void)world.Remove<PhysicsBodyComponent>(entity); // no-op for dead entities
                      return true;
                  });
    if (!removed.empty())
    {
        JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
        bodies.RemoveBodies(removed.data(), static_cast<int>(removed.size()));
        bodies.DestroyBodies(removed.data(), static_cast<int>(removed.size()));
    }
}

void PhysicsSync::CreateBodies(World& world)
{
    INTERI_ZONE_NAMED("PhysicsSync::CreateBodies");
    JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
    std::vector<std::pair<Entity, JPH::BodyID>> created;
    std::vector<JPH::BodyID> activate;
    std::vector<JPH::BodyID> asleep;
    m_toCreate.ForEachChunk(world,
                            [&](ChunkView& chunk)
                            {
                                const auto entities = chunk.Entities();
                                const auto rigidBodies = chunk.Read<RigidBodyComponent>();
                                const auto colliders = chunk.Read<ColliderComponent>();
                                const auto transforms = chunk.Read<Transform>();
                                for (std::uint32_t row = 0; row < chunk.Count(); ++row)
                                {
                                    const RigidBodyComponent& rigidBody = rigidBodies[row];
                                    INTERI_ASSERT(colliders[row].shape != nullptr, "ColliderComponent has no shape");
                                    JPH::Body* body = bodies.CreateBody(
                                        MakeSettings(rigidBody, colliders[row], transforms[row], entities[row]));
                                    if (body == nullptr)
                                    {
                                        INTERI_LOG_ERROR("Physics", "out of bodies (PhysicsWorldDesc::maxBodies)");
                                        return;
                                    }
                                    created.emplace_back(entities[row], body->GetID());
                                    const bool awake = rigidBody.motion != MotionType::Static && !rigidBody.startAsleep;
                                    (awake ? activate : asleep).push_back(body->GetID());
                                }
                            });
    AddBatch(bodies, activate, JPH::EActivation::Activate);
    AddBatch(bodies, asleep, JPH::EActivation::DontActivate);
    for (const auto& [entity, id] : created)
    {
        world.Add(entity, PhysicsBodyComponent{.id = id, .wasActive = false});
        m_bodies.emplace_back(entity, id);
    }
}

void PhysicsSync::PushKinematic(World& world, float deltaSeconds)
{
    INTERI_ZONE_NAMED("PhysicsSync::PushKinematic");
    JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
    m_withBody.ForEachChunk(world, RunTicks{.lastRun = 0, .thisRun = world.BeginRun()},
                            [&](ChunkView& chunk)
                            {
                                const auto rigidBodies = chunk.Read<RigidBodyComponent>();
                                const auto handles = chunk.Read<PhysicsBodyComponent>();
                                const auto transforms = chunk.Read<Transform>();
                                for (std::uint32_t row = 0; row < chunk.Count(); ++row)
                                {
                                    if (rigidBodies[row].motion == MotionType::Kinematic)
                                    {
                                        bodies.MoveKinematic(handles[row].id, ToJolt(transforms[row].position),
                                                             ToJolt(transforms[row].rotation), deltaSeconds);
                                    }
                                }
                            });
}

void PhysicsSync::PullTransforms(World& world, JobSystem& jobs)
{
    INTERI_ZONE_NAMED("PhysicsSync::PullTransforms");
    const JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
    m_withBody.ParallelForEachChunk(
        world, jobs, RunTicks{.lastRun = 0, .thisRun = world.BeginRun()},
        [&](ChunkView& chunk, std::size_t)
        {
            const auto rigidBodies = chunk.Read<RigidBodyComponent>();
            const auto handles = chunk.Read<PhysicsBodyComponent>();
            const std::uint32_t count = chunk.Count();
            const auto needsPull = [&](std::uint32_t row)
            {
                return rigidBodies[row].motion == MotionType::Dynamic &&
                       (handles[row].wasActive || bodies.IsActive(handles[row].id));
            };
            bool any = false;
            for (std::uint32_t row = 0; row < count && !any; ++row)
            {
                any = needsPull(row);
            }
            if (!any)
            {
                return; // every body slept through the step: leave the chunk (and its change ticks) alone
            }
            const auto writableHandles = chunk.Write<PhysicsBodyComponent>();
            const auto transforms = chunk.Write<Transform>();
            for (std::uint32_t row = 0; row < count; ++row)
            {
                if (!needsPull(row))
                {
                    continue;
                }
                JPH::RVec3 position;
                JPH::Quat rotation;
                bodies.GetPositionAndRotation(writableHandles[row].id, position, rotation);
                transforms[row].position = FromJolt(position);
                transforms[row].rotation = FromJolt(rotation);
                writableHandles[row].wasActive = bodies.IsActive(writableHandles[row].id);
            }
        });
}

} // namespace interi::physics

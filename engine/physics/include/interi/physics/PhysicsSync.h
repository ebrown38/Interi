// SPDX-License-Identifier: MIT
#pragma once

#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsWorld.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
// clang-format on

#include <cstddef>
#include <utility>
#include <vector>

namespace interi::physics
{

/// Keeps Jolt bodies in step with ECS entities that have RigidBodyComponent + ColliderComponent + interi::Transform.
///
/// Every function here is a sync point: no systems may run on `world` and nothing else may touch the PhysicsWorld.
/// Bodies are created and removed on the calling thread in query (archetype, chunk, row) order, so body IDs and the
/// simulation stay deterministic at any thread count; only the transform pull runs in parallel.
///
/// Each body's user data is its entity (PhysicsWorld::EntityOf). The destructor removes every body this created.
class PhysicsSync
{
public:
    explicit PhysicsSync(PhysicsWorld& physics);
    ~PhysicsSync();

    PhysicsSync(const PhysicsSync&) = delete;
    PhysicsSync& operator=(const PhysicsSync&) = delete;
    PhysicsSync(PhysicsSync&&) = delete;
    PhysicsSync& operator=(PhysicsSync&&) = delete;

    /// One fixed step: DestroyRemovedBodies, CreateBodies, PushKinematic, PhysicsWorld::Step, PullTransforms.
    void Step(World& world, JobSystem& jobs, float deltaSeconds);

    /// Removes the bodies of entities that were destroyed or lost their RigidBodyComponent (and the entity's
    /// PhysicsBodyComponent, if it is still alive).
    void DestroyRemovedBodies(World& world);

    /// Creates bodies for entities that have the three components but no PhysicsBodyComponent yet, and adds one.
    void CreateBodies(World& world);

    /// Moves kinematic bodies towards their entity's Transform over `deltaSeconds`.
    void PushKinematic(World& world, float deltaSeconds);

    /// Writes the pose of dynamic bodies that are active (or fell asleep in the last step) into their Transform,
    /// in parallel over chunks. Chunks whose bodies all slept through the step are not written, so their change
    /// ticks stay as they were.
    void PullTransforms(World& world, JobSystem& jobs);

    [[nodiscard]] std::size_t BodyCount() const noexcept { return m_bodies.size(); }

private:
    PhysicsWorld* m_physics;
    std::vector<std::pair<Entity, JPH::BodyID>> m_bodies; ///< In creation order.
    Query m_toCreate;
    Query m_withBody;
};

} // namespace interi::physics

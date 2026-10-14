// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/testing/ExpectNear.h>

#include "PhysicsTestUtil.h"
#include <gtest/gtest.h>

#include <cstdint>

using interi::Entity;
using interi::JobSystem;
using interi::Quat;
using interi::Transform;
using interi::Vec3;
using interi::World;
using namespace interi::physics;
using namespace interi::physics::testing;

namespace
{

constexpr float kDt = 1.0f / 60.0f;

/// Puts entities into a separate archetype (and so separate chunks).
struct OtherArchetypeTag
{
};

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

Entity SpawnBox(World& world, const Vec3& position, RigidBodyComponent body = {})
{
    return world.CreateEntity(body, ColliderComponent::Box(Vec3(0.5f)), At(position));
}

class PhysicsSyncTest : public ::testing::Test
{
protected:
    JobSystem m_jobs{Workers(3)};
    PhysicsWorld m_physics{m_jobs};
    World m_world;
    PhysicsSync m_sync{m_physics};
};

} // namespace

TEST_F(PhysicsSyncTest, CreateBodies_AddsBodyComponentAndUserData)
{
    RigidBodyComponent body;
    body.layer = ObjectLayer::DebrisSmall;
    body.mass = 7.0f;
    const Entity entity = SpawnBox(m_world, Vec3(1.0f, 2.0f, 3.0f), body);

    m_sync.CreateBodies(m_world);

    const PhysicsBodyComponent* handle = m_world.Get<PhysicsBodyComponent>(entity);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(m_sync.BodyCount(), 1u);
    EXPECT_EQ(m_physics.EntityOf(handle->id), entity);
    JPH::BodyInterface& bodies = m_physics.Bodies();
    EXPECT_TRUE(bodies.IsAdded(handle->id));
    EXPECT_TRUE(bodies.IsActive(handle->id));
    EXPECT_EQ(bodies.GetObjectLayer(handle->id), ToJolt(ObjectLayer::DebrisSmall));
    EXPECT_EQ(FromJolt(bodies.GetPosition(handle->id)), Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(
        1.0f /
            m_physics.System().GetBodyLockInterface().TryGetBody(handle->id)->GetMotionProperties()->GetInverseMass(),
        7.0f, 1e-4f);

    m_sync.CreateBodies(m_world); // already has a body: nothing new
    EXPECT_EQ(m_sync.BodyCount(), 1u);
}

TEST_F(PhysicsSyncTest, InitialVelocity_Applied)
{
    RigidBodyComponent body;
    body.linearVelocity = Vec3(3.0f, 4.0f, 0.0f);
    const Entity entity = SpawnBox(m_world, Vec3(0.0f, 5.0f, 0.0f), body);
    m_sync.CreateBodies(m_world);
    EXPECT_EQ(FromJolt(m_physics.Bodies().GetLinearVelocity(m_world.Get<PhysicsBodyComponent>(entity)->id)),
              Vec3(3.0f, 4.0f, 0.0f));
}

TEST_F(PhysicsSyncTest, StartAsleepAndStatic_NotActive)
{
    RigidBodyComponent asleep;
    asleep.startAsleep = true;
    RigidBodyComponent fixed;
    fixed.motion = MotionType::Static;
    fixed.layer = ObjectLayer::Structure;
    const Entity a = SpawnBox(m_world, Vec3(0.0f, 5.0f, 0.0f), asleep);
    const Entity b = SpawnBox(m_world, Vec3(3.0f, 5.0f, 0.0f), fixed);
    m_sync.CreateBodies(m_world);
    EXPECT_FALSE(m_physics.Bodies().IsActive(m_world.Get<PhysicsBodyComponent>(a)->id));
    EXPECT_FALSE(m_physics.Bodies().IsActive(m_world.Get<PhysicsBodyComponent>(b)->id));
}

TEST_F(PhysicsSyncTest, RigidBodySync_EntityDestroyed_BodyRemoved)
{
    const Entity keep = SpawnBox(m_world, Vec3(0.0f, 1.0f, 0.0f));
    const Entity doomed = SpawnBox(m_world, Vec3(3.0f, 1.0f, 0.0f));
    m_sync.CreateBodies(m_world);
    const JPH::BodyID doomedBody = m_world.Get<PhysicsBodyComponent>(doomed)->id;
    ASSERT_EQ(m_physics.System().GetNumBodies(), 2u);

    ASSERT_TRUE(m_world.DestroyEntity(doomed));
    m_sync.DestroyRemovedBodies(m_world);

    EXPECT_EQ(m_sync.BodyCount(), 1u);
    EXPECT_EQ(m_physics.System().GetNumBodies(), 1u);
    EXPECT_FALSE(m_physics.Bodies().IsAdded(doomedBody));
    EXPECT_TRUE(m_physics.Bodies().IsAdded(m_world.Get<PhysicsBodyComponent>(keep)->id));
}

TEST_F(PhysicsSyncTest, RigidBodyRemoved_BodyAndHandleRemoved)
{
    const Entity entity = SpawnBox(m_world, Vec3(0.0f, 1.0f, 0.0f));
    m_sync.CreateBodies(m_world);
    ASSERT_TRUE(m_world.Remove<RigidBodyComponent>(entity));
    m_sync.DestroyRemovedBodies(m_world);
    EXPECT_EQ(m_physics.System().GetNumBodies(), 0u);
    EXPECT_FALSE(m_world.Has<PhysicsBodyComponent>(entity));

    // Adding it back creates a new body.
    m_world.Add(entity, RigidBodyComponent{});
    m_sync.CreateBodies(m_world);
    EXPECT_EQ(m_physics.System().GetNumBodies(), 1u);
}

TEST_F(PhysicsSyncTest, Destructor_RemovesAllBodies)
{
    {
        PhysicsSync sync(m_physics);
        SpawnBox(m_world, Vec3(0.0f, 1.0f, 0.0f));
        sync.CreateBodies(m_world);
        ASSERT_EQ(m_physics.System().GetNumBodies(), 1u);
    }
    EXPECT_EQ(m_physics.System().GetNumBodies(), 0u);
}

TEST_F(PhysicsSyncTest, Step_FallingBodyTransformFollowsJolt)
{
    AddGround(m_physics);
    const Entity entity = SpawnBox(m_world, Vec3(0.0f, 3.0f, 0.0f));
    m_sync.Step(m_world, m_jobs, kDt);
    const float afterOneStep = m_world.Get<Transform>(entity)->position.y;
    EXPECT_LT(afterOneStep, 3.0f);

    // Run until the box has settled and fallen asleep: the pose it slept at must be pulled exactly.
    const JPH::BodyID id = m_world.Get<PhysicsBodyComponent>(entity)->id;
    for (int i = 0; i < 600 && m_physics.Bodies().IsActive(id); ++i)
    {
        m_sync.Step(m_world, m_jobs, kDt);
    }
    ASSERT_FALSE(m_physics.Bodies().IsActive(id));
    const Transform& transform = *m_world.Get<Transform>(entity);
    EXPECT_EQ(transform.position, FromJolt(m_physics.Bodies().GetPosition(id)));
    EXPECT_EQ(transform.rotation, FromJolt(m_physics.Bodies().GetRotation(id)));
    EXPECT_NEAR(transform.position.y, 0.5f, 0.03f);
    EXPECT_EQ(transform.scale, Vec3(1.0f)); // scale is the entity's, untouched
}

TEST_F(PhysicsSyncTest, TransformPull_OnlyActiveBodiesWritten)
{
    RigidBodyComponent asleep;
    asleep.startAsleep = true;
    // Different archetypes, so each body sits in its own chunk.
    const Entity sleeper = SpawnBox(m_world, Vec3(0.0f, 5.0f, 0.0f), asleep);
    const Entity faller = m_world.CreateEntity(RigidBodyComponent{}, ColliderComponent::Box(Vec3(0.5f)),
                                               At(Vec3(5.0f, 5.0f, 0.0f)), OtherArchetypeTag{});
    // A sleeper sharing the faller's chunk: written chunk, but its row must keep its value.
    const Entity chunkMate = m_world.CreateEntity(asleep, ColliderComponent::Box(Vec3(0.5f)),
                                                  At(Vec3(10.0f, 5.0f, 0.0f)), OtherArchetypeTag{});
    m_sync.CreateBodies(m_world);

    // Make the ECS transforms differ from the bodies' poses, so a write would be visible.
    m_world.GetMut<Transform>(sleeper)->position = Vec3(100.0f, 0.0f, 0.0f);
    m_world.GetMut<Transform>(chunkMate)->position = Vec3(200.0f, 0.0f, 0.0f);
    interi::Query changed;
    changed.Changed<Transform>();
    changed.ForEachChunk(m_world, [](interi::ChunkView&) {}); // consume the changes made so far

    m_physics.Step(kDt);
    m_sync.PullTransforms(m_world, m_jobs);

    EXPECT_EQ(m_world.Get<Transform>(sleeper)->position, Vec3(100.0f, 0.0f, 0.0f));
    EXPECT_EQ(m_world.Get<Transform>(chunkMate)->position, Vec3(200.0f, 0.0f, 0.0f));
    EXPECT_LT(m_world.Get<Transform>(faller)->position.y, 5.0f);

    // Only the faller's chunk was stamped as changed.
    int changedChunks = 0;
    bool fallerSeen = false;
    changed.ForEachChunk(m_world,
                         [&](interi::ChunkView& chunk)
                         {
                             ++changedChunks;
                             for (const Entity e : chunk.Entities())
                             {
                                 fallerSeen = fallerSeen || e == faller;
                             }
                         });
    EXPECT_EQ(changedChunks, 1);
    EXPECT_TRUE(fallerSeen);
}

TEST_F(PhysicsSyncTest, Kinematic_FollowsTransform)
{
    RigidBodyComponent body;
    body.motion = MotionType::Kinematic;
    body.layer = ObjectLayer::Structure;
    const Entity entity = SpawnBox(m_world, Vec3(0.0f, 1.0f, 0.0f), body);
    m_sync.Step(m_world, m_jobs, kDt);

    const Quat turn = Quat::FromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), 0.5f);
    m_world.GetMut<Transform>(entity)->position = Vec3(1.0f, 1.0f, 0.0f);
    m_world.GetMut<Transform>(entity)->rotation = turn;
    m_sync.Step(m_world, m_jobs, kDt);

    const JPH::BodyID id = m_world.Get<PhysicsBodyComponent>(entity)->id;
    INTERI_EXPECT_VEC_NEAR(FromJolt(m_physics.Bodies().GetPosition(id)), Vec3(1.0f, 1.0f, 0.0f), 1e-4f);
    INTERI_EXPECT_QUAT_NEAR(FromJolt(m_physics.Bodies().GetRotation(id)), turn, 1e-4f);
    // The kinematic body reached its target, and its transform isn't overwritten by the pull.
    EXPECT_EQ(m_world.Get<Transform>(entity)->position, Vec3(1.0f, 1.0f, 0.0f));

    // Not moved again: it stops instead of coasting on the last step's velocity.
    m_sync.Step(m_world, m_jobs, kDt);
    INTERI_EXPECT_VEC_NEAR(FromJolt(m_physics.Bodies().GetPosition(id)), Vec3(1.0f, 1.0f, 0.0f), 1e-4f);
}

TEST(PhysicsSync, EcsDrivenScene_DeterministicAcrossThreadCounts)
{
    const auto simulate = [](std::uint32_t workers)
    {
        JobSystem jobs(Workers(workers));
        PhysicsWorld physics(jobs);
        World world;
        PhysicsSync sync(physics);
        AddGround(physics);
        for (int i = 0; i < 40; ++i)
        {
            RigidBodyComponent body;
            body.layer = i % 2 == 0 ? ObjectLayer::Debris : ObjectLayer::DebrisSmall;
            const float x = static_cast<float>(i % 3) * 0.3f;
            SpawnBox(world, Vec3(x, 0.6f + 1.05f * static_cast<float>(i), 0.1f * static_cast<float>(i % 5)), body);
        }
        for (int step = 0; step < 60; ++step)
        {
            sync.Step(world, jobs, kDt);
        }
        return HashState(physics);
    };
    EXPECT_EQ(simulate(0), simulate(7));
}

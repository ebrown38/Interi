// SPDX-License-Identifier: MIT
// ECS-driven scenes stepped through PhysicsSync at different thread counts: the simulation and the merged contact
// events must be identical.
#include <interi/core/math/Transform.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/ContactEvents.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/testing/StateHasher.h>

#include "../PhysicsTestUtil.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

using interi::Entity;
using interi::JobSystem;
using interi::Transform;
using interi::Vec3;
using interi::World;
using namespace interi::physics;
using namespace interi::physics::testing;

namespace
{

constexpr float kDt = 1.0f / 60.0f;

Entity SpawnBox(World& world, const Vec3& position, RigidBodyComponent body = {})
{
    Transform transform;
    transform.position = position;
    return world.CreateEntity(body, ColliderComponent::Box(Vec3(0.5f)), transform);
}

Entity SpawnBox(World& world, const Vec3& position, float mass)
{
    RigidBodyComponent body;
    body.mass = mass;
    return SpawnBox(world, position, body);
}

} // namespace

TEST(ContactEvents, Pile_IdenticalAcrossThreadCounts)
{
    const auto simulate = [](std::uint32_t workers)
    {
        JobSystem jobs(Workers(workers));
        PhysicsWorld physics(jobs);
        World world;
        PhysicsSync sync(physics);
        AddGround(physics);
        for (int i = 0; i < 48; ++i)
        {
            SpawnBox(world,
                     Vec3(static_cast<float>(i % 4) * 0.7f, 1.0f + 1.1f * static_cast<float>(i / 4),
                          static_cast<float>(i % 3) * 0.3f),
                     5.0f);
        }
        interi::testing::StateHasher hasher;
        std::size_t count = 0;
        for (int step = 0; step < 90; ++step)
        {
            sync.Step(world, jobs, kDt);
            for (const ContactEvent& e : physics.Contacts())
            {
                hasher.Add(e.bodyA.GetIndexAndSequenceNumber());
                hasher.Add(e.bodyB.GetIndexAndSequenceNumber());
                hasher.Add(e.point.x);
                hasher.Add(e.point.y);
                hasher.Add(e.point.z);
                hasher.Add(e.impulse);
            }
            count += physics.Contacts().size();
        }
        EXPECT_GT(count, 48u);
        return hasher.Finish();
    };
    const std::uint64_t reference = simulate(0);
    EXPECT_EQ(simulate(3), reference);
    EXPECT_EQ(simulate(7), reference);
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

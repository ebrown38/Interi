// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/ContactEvents.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/testing/StateHasher.h>

#include "PhysicsTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

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

Entity SpawnBox(World& world, const Vec3& position, float mass)
{
    RigidBodyComponent body;
    body.mass = mass;
    Transform transform;
    transform.position = position;
    return world.CreateEntity(body, ColliderComponent::Box(Vec3(0.5f)), transform);
}

/// Steps until the first step that reports contacts, returning them.
std::vector<ContactEvent> FirstContacts(PhysicsSync& sync, World& world, JobSystem& jobs, PhysicsWorld& physics)
{
    for (int i = 0; i < 300; ++i)
    {
        sync.Step(world, jobs, kDt);
        if (!physics.Contacts().empty())
        {
            return {physics.Contacts().begin(), physics.Contacts().end()};
        }
    }
    return {};
}

} // namespace

TEST(ContactListener, HighImpulse_EventRecorded)
{
    JobSystem jobs(Workers(3));
    PhysicsWorldDesc desc;
    desc.minContactImpulse = 50.0f;
    PhysicsWorld physics(jobs, desc);
    World world;
    PhysicsSync sync(physics);
    const JPH::BodyID ground = AddGround(physics);
    constexpr float kMass = 100.0f;
    constexpr float kDrop = 5.0f; // bottom face 5 m above the ground
    const Entity box = SpawnBox(world, Vec3(0.0f, kDrop + 0.5f, 0.0f), kMass);

    const std::vector<ContactEvent> contacts = FirstContacts(sync, world, jobs, physics);

    ASSERT_EQ(contacts.size(), 1u);
    const ContactEvent& hit = contacts[0];
    const JPH::BodyID boxBody = world.Get<PhysicsBodyComponent>(box)->id;
    // Ground was created first, so it has the lower ID and is body A.
    EXPECT_EQ(hit.bodyA, ground);
    EXPECT_EQ(hit.bodyB, boxBody);
    EXPECT_FALSE(hit.entityA.IsValid()); // the ground isn't an ECS entity
    EXPECT_EQ(hit.entityB, box);
    EXPECT_NEAR(hit.normal.y, 1.0f, 1e-3f); // from the ground (A) up to the box (B)
    EXPECT_NEAR(hit.point.y, 0.0f, 0.1f);
    // Free fall from 5 m: v = sqrt(2 g h) ~ 9.9 m/s, so ~990 N·s against the static ground. Jolt detects the
    // contact up to one step early (speculative contacts), so allow for one step less of fall.
    const float speed = std::sqrt(2.0f * 9.81f * kDrop);
    EXPECT_NEAR(hit.impulse, kMass * speed, kMass * 9.81f * kDt * 2.0f);
}

TEST(ContactListener, LowImpulse_Filtered)
{
    JobSystem jobs(Workers(1));
    PhysicsWorldDesc desc;
    desc.minContactImpulse = 50.0f;
    PhysicsWorld physics(jobs, desc);
    World world;
    PhysicsSync sync(physics);
    AddGround(physics);
    SpawnBox(world, Vec3(0.0f, 0.52f, 0.0f), 10.0f); // 2 cm drop: ~6 N·s
    for (int i = 0; i < 60; ++i)
    {
        sync.Step(world, jobs, kDt);
        EXPECT_TRUE(physics.Contacts().empty()) << "step " << i;
    }
}

TEST(ContactListener, PersistentContact_ReportedOnce)
{
    JobSystem jobs(Workers(1));
    PhysicsWorld physics(jobs); // default threshold: every new contact
    World world;
    PhysicsSync sync(physics);
    AddGround(physics);
    SpawnBox(world, Vec3(0.0f, 1.5f, 0.0f), 10.0f);
    std::size_t total = 0;
    for (int i = 0; i < 120; ++i)
    {
        sync.Step(world, jobs, kDt);
        total += physics.Contacts().size();
    }
    EXPECT_EQ(total, 1u);
}

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

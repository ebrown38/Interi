// SPDX-License-Identifier: MIT
// A seeded destruction-like scene (a tower of debris boxes, rubble and a projectile) stepped on different thread
// counts must end in bit-identical state.
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/testing/DeterministicRng.h>

#include "../PhysicsTestUtil.h"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
// clang-format on

#include <gtest/gtest.h>

#include <cstdint>

using interi::JobSystem;
using interi::physics::ObjectLayer;
using interi::physics::PhysicsWorld;
using interi::physics::ToJolt;
using namespace interi::physics::testing;

namespace
{

std::uint64_t SimulateScene(std::uint64_t seed, std::uint32_t threads, int steps = 90)
{
    JobSystem jobs(Workers(threads - 1));
    PhysicsWorld world(jobs);
    AddGround(world);

    interi::testing::DeterministicRng rng(seed);
    JPH::BodyInterface& bodies = world.Bodies();
    for (int i = 0; i < 200; ++i)
    {
        const bool small = i % 3 == 0;
        const float half = small ? rng.NextFloat(0.1f, 0.2f) : rng.NextFloat(0.3f, 0.6f);
        // A loose column: four bodies per level, jittered so the pile topples.
        const JPH::RVec3 position(rng.NextFloat(-0.4f, 0.4f), 0.5f + 0.3f * static_cast<float>(i / 4),
                                  rng.NextFloat(-0.4f, 0.4f));
        const JPH::BodyCreationSettings settings(new JPH::BoxShape(JPH::Vec3(half, half, half)), position,
                                                 JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rng.NextFloat(0.0f, 3.0f)),
                                                 JPH::EMotionType::Dynamic,
                                                 ToJolt(small ? ObjectLayer::DebrisSmall : ObjectLayer::Debris));
        bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    }
    JPH::BodyCreationSettings projectile(new JPH::SphereShape(0.2f), JPH::RVec3(-20.0f, 3.0f, 0.0f),
                                         JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic,
                                         ToJolt(ObjectLayer::Projectile));
    projectile.mLinearVelocity = JPH::Vec3(60.0f, 0.0f, 0.0f);
    projectile.mMotionQuality = JPH::EMotionQuality::LinearCast;
    bodies.CreateAndAddBody(projectile, JPH::EActivation::Activate);
    world.OptimizeBroadPhase();

    for (int step = 0; step < steps; ++step)
    {
        world.Step(1.0f / 60.0f);
    }
    return HashState(world);
}

} // namespace

TEST(PhysicsWorld, StateHash_ChangesWithSeedAndSteps)
{
    EXPECT_NE(SimulateScene(1, 4), SimulateScene(2, 4));
    EXPECT_NE(SimulateScene(1, 4, 89), SimulateScene(1, 4, 90));
}

TEST(PhysicsWorld, SameSeed_DeterministicAcrossThreadCounts)
{
    const std::uint64_t reference = SimulateScene(42, 1);
    for (const std::uint32_t threads : {8u, 32u})
    {
        EXPECT_EQ(SimulateScene(42, threads), reference) << threads << " threads";
    }
}

// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/PhysicsWorld.h>

#include "PhysicsTestUtil.h"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
// clang-format on

#include <gtest/gtest.h>

using interi::JobSystem;
using interi::physics::ObjectLayer;
using interi::physics::PhysicsWorld;
using interi::physics::ToJolt;
using interi::physics::testing::AddGround;
using interi::physics::testing::Workers;

namespace
{

JPH::BodyID AddSphere(PhysicsWorld& world, ObjectLayer layer, JPH::RVec3 position, float radius = 0.5f)
{
    const JPH::BodyCreationSettings settings(new JPH::SphereShape(radius), position, JPH::Quat::sIdentity(),
                                             JPH::EMotionType::Dynamic, ToJolt(layer));
    return world.Bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
}

void StepSeconds(PhysicsWorld& world, float seconds)
{
    constexpr float kDt = 1.0f / 60.0f;
    const auto steps = static_cast<int>(seconds / kDt + 0.5f);
    for (int i = 0; i < steps; ++i)
    {
        world.Step(kDt);
    }
}

} // namespace

TEST(PhysicsWorld, DropSphere_RestsOnGround)
{
    JobSystem jobs(Workers(2));
    PhysicsWorld world(jobs);
    AddGround(world);
    const JPH::BodyID sphere = AddSphere(world, ObjectLayer::Debris, JPH::RVec3(0.0f, 5.0f, 0.0f));

    StepSeconds(world, 5.0f);

    // Resting contacts settle up to Jolt's penetration slop into the ground (radius 0.5 above the top face at 0).
    const float slop = world.System().GetPhysicsSettings().mPenetrationSlop;
    const JPH::RVec3 position = world.Bodies().GetCenterOfMassPosition(sphere);
    EXPECT_GE(position.GetY(), 0.5f - slop - 1e-3f);
    EXPECT_LE(position.GetY(), 0.5f + 1e-3f);
    EXPECT_FALSE(world.Bodies().IsActive(sphere)) << "a resting body should have gone to sleep";
    EXPECT_NEAR(position.GetX(), 0.0f, 1e-3f);
    EXPECT_NEAR(position.GetZ(), 0.0f, 1e-3f);
    EXPECT_EQ(world.StepCount(), 300u);
}

TEST(PhysicsWorld, Gravity_FromDesc)
{
    JobSystem jobs(Workers(0));
    interi::physics::PhysicsWorldDesc desc;
    desc.gravity = interi::Vec3(0.0f, 0.0f, 0.0f);
    PhysicsWorld world(jobs, desc);
    const JPH::BodyID sphere = AddSphere(world, ObjectLayer::Debris, JPH::RVec3(0.0f, 5.0f, 0.0f));
    StepSeconds(world, 1.0f);
    EXPECT_EQ(world.Bodies().GetCenterOfMassPosition(sphere).GetY(), 5.0f);
}

TEST(PhysicsWorld, DebrisSmallPair_PassThroughEachOther)
{
    JobSystem jobs(Workers(2));
    interi::physics::PhysicsWorldDesc desc;
    desc.gravity = interi::Vec3(0.0f, 0.0f, 0.0f);
    PhysicsWorld world(jobs, desc);
    // Two pieces of rubble flying at each other: they must not collide, so each keeps its velocity.
    const JPH::BodyID left = AddSphere(world, ObjectLayer::DebrisSmall, JPH::RVec3(-2.0f, 0.0f, 0.0f));
    const JPH::BodyID right = AddSphere(world, ObjectLayer::DebrisSmall, JPH::RVec3(2.0f, 0.0f, 0.0f));
    world.Bodies().SetLinearVelocity(left, JPH::Vec3(4.0f, 0.0f, 0.0f));
    world.Bodies().SetLinearVelocity(right, JPH::Vec3(-4.0f, 0.0f, 0.0f));
    StepSeconds(world, 1.0f);
    EXPECT_GT(world.Bodies().GetCenterOfMassPosition(left).GetX(), 1.5f);
    EXPECT_LT(world.Bodies().GetCenterOfMassPosition(right).GetX(), -1.5f);
}

TEST(PhysicsWorld, DebrisPair_Collide)
{
    JobSystem jobs(Workers(2));
    interi::physics::PhysicsWorldDesc desc;
    desc.gravity = interi::Vec3(0.0f, 0.0f, 0.0f);
    PhysicsWorld world(jobs, desc);
    const JPH::BodyID left = AddSphere(world, ObjectLayer::Debris, JPH::RVec3(-2.0f, 0.0f, 0.0f));
    const JPH::BodyID right = AddSphere(world, ObjectLayer::Debris, JPH::RVec3(2.0f, 0.0f, 0.0f));
    world.Bodies().SetLinearVelocity(left, JPH::Vec3(4.0f, 0.0f, 0.0f));
    world.Bodies().SetLinearVelocity(right, JPH::Vec3(-4.0f, 0.0f, 0.0f));
    StepSeconds(world, 1.0f);
    EXPECT_LT(world.Bodies().GetCenterOfMassPosition(left).GetX(), 0.0f);
    EXPECT_GT(world.Bodies().GetCenterOfMassPosition(right).GetX(), 0.0f);
}

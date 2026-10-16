// SPDX-License-Identifier: MIT
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/gameplay/Vehicle.h>
#include <interi/physics/Components.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>

using interi::Entity;
using interi::Quat;
using interi::Transform;
using interi::Vec3;
using interi::destruction::DestructibleId;
using interi::destruction::DestructionWorld;
using interi::gameplay::Vehicle;
using interi::gameplay::VehicleComponent;
using interi::gameplay::VehicleDef;
using interi::gameplay::VehicleInput;
using interi::gameplay::WreckComponent;
using interi::physics::MotionType;
using interi::physics::ObjectLayer;
using interi::physics::RigidBodyComponent;
using interi::testing::GameplayScene;

namespace
{

/// One forward gear, so the top speed is analytical: the engine's max RPM through gear and differential.
VehicleDef OneGear()
{
    VehicleDef def;
    def.name = "test";
    def.gearRatios = {1.0f};
    def.differentialRatio = 3.0f;
    def.engineMinRpm = 500.0f;
    def.engineMaxRpm = 1500.0f;
    return def;
}

float TopSpeed(const VehicleDef& def)
{
    const float wheelRadPerSecond =
        def.engineMaxRpm * 2.0f * std::numbers::pi_v<float> / 60.0f / (def.gearRatios[0] * def.differentialRatio);
    return wheelRadPerSecond * def.wheelRadius;
}

Transform At(const Vec3& position)
{
    Transform pose;
    pose.position = position;
    return pose;
}

class VehicleTest : public ::testing::Test
{
protected:
    VehicleTest() { scene.AddGround(); }

    void Step(Vehicle& vehicle)
    {
        scene.Step();
        vehicle.Update();
        destruction.Update(scene.world, scene.jobs);
    }

    void Steps(Vehicle& vehicle, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            Step(vehicle);
        }
    }

    GameplayScene scene;
    DestructionWorld destruction{scene.physics};
};

} // namespace

TEST_F(VehicleTest, Spawn_SettlesOnWheels)
{
    Vehicle vehicle(scene.physics, scene.world, OneGear(), At(Vec3(0.0f, 1.5f, 0.0f)));
    Steps(vehicle, 120);
    EXPECT_FALSE(vehicle.IsDestroyed());
    for (std::uint32_t wheel = 0; wheel < vehicle.WheelCount(); ++wheel)
    {
        EXPECT_TRUE(vehicle.WheelInContact(wheel)) << wheel;
    }
    EXPECT_NEAR(Length(vehicle.LinearVelocity()), 0.0f, 0.05f);
    // The entity follows the chassis.
    const Transform* pose = scene.world.Get<Transform>(vehicle.GetEntity());
    ASSERT_NE(pose, nullptr);
    EXPECT_NEAR(pose->position.y, vehicle.Pose().position.y, 1e-5f);
    EXPECT_NE(scene.world.Get<VehicleComponent>(vehicle.GetEntity()), nullptr);
    EXPECT_EQ(scene.physics.EntityOf(vehicle.BodyId()), vehicle.GetEntity());
}

TEST_F(VehicleTest, Accelerate_ReachesExpectedSpeed)
{
    VehicleDef def = OneGear();
    def.engineMaxRpm = 1000.0f;                                             // top speed ~14 m/s
    def.engineTorque = 1500.0f;                                             // reached within a few seconds
    scene.AddStaticBox(Vec3(0.0f, -0.5f, 0.0f), Vec3(20.0f, 0.5f, 500.0f)); // a long road
    Vehicle vehicle(scene.physics, scene.world, def, At(Vec3(0.0f, 1.5f, -400.0f)));
    Steps(vehicle, 60);
    vehicle.SetInput({.forward = 1.0f});
    Steps(vehicle, 840);
    const float at14s = vehicle.ForwardSpeed();
    Steps(vehicle, 120);
    const float at16s = vehicle.ForwardSpeed();

    const float expected = TopSpeed(def);
    EXPECT_NEAR(at16s, expected, 0.1f * expected);
    EXPECT_LT(at16s, 1.01f * expected);
    EXPECT_NEAR(at16s, at14s, 0.03f * expected); // steady state
    EXPECT_NEAR(vehicle.EngineRpm(), def.engineMaxRpm, 0.05f * def.engineMaxRpm);
    // Straight ahead along +Z (~200 m).
    EXPECT_GT(vehicle.Pose().position.z, -250.0f);
    EXPECT_NEAR(vehicle.Pose().position.x, 0.0f, 2.0f);
}

TEST_F(VehicleTest, Brake_Stops)
{
    Vehicle vehicle(scene.physics, scene.world, OneGear(), At(Vec3(0.0f, 1.5f, -40.0f)));
    Steps(vehicle, 60);
    vehicle.SetInput({.forward = 1.0f});
    Steps(vehicle, 180);
    ASSERT_GT(vehicle.ForwardSpeed(), 4.0f);
    vehicle.SetInput({.brake = 1.0f});
    Steps(vehicle, 240);
    EXPECT_NEAR(vehicle.ForwardSpeed(), 0.0f, 0.2f);
}

TEST_F(VehicleTest, Steer_TurnsRight)
{
    Vehicle vehicle(scene.physics, scene.world, OneGear(), At(Vec3(0.0f, 1.5f, 0.0f)));
    Steps(vehicle, 60);
    vehicle.SetInput({.forward = 0.6f, .right = 1.0f});
    Steps(vehicle, 180);
    // Vehicle space is +Z forward, +X left: a right turn heads towards -X.
    EXPECT_LT(vehicle.Pose().position.x, -1.0f);
    const Vec3 forward = vehicle.Pose().TransformVector(Vec3(0.0f, 0.0f, 1.0f));
    EXPECT_LT(forward.x, -0.3f);
}

TEST_F(VehicleTest, RamWall_DamageScalesWithSpeed)
{
    // A tough wall (nothing breaks, so health loss is not capped) of unit cubes x in [0, 4), y in [0, 3), z in
    // [0, 1); the vehicle starts just in front of it, heading +Z, and is given its speed directly.
    interi::destruction::testing::GridAssetOptions tough;
    tough.fractureEnergy = 1.0e7f;
    const auto asset = std::make_shared<const interi::destruction::DestructibleAsset>(
        interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(4, 3, 1), tough));

    const auto ram = [&](float speed)
    {
        GameplayScene local;
        local.AddGround();
        DestructionWorld world(local.physics);
        const DestructibleId wall = world.Spawn(local.world, local.jobs, asset, Transform{});
        const VehicleDef def = OneGear();
        Vehicle vehicle(local.physics, local.world, def, At(Vec3(2.0f, 1.5f, -6.0f)));
        const auto step = [&]
        {
            local.Step();
            vehicle.Update();
            world.Update(local.world, local.jobs);
        };
        for (int i = 0; i < 60; ++i)
        {
            step();
        }
        local.physics.Bodies().SetLinearVelocity(vehicle.BodyId(), JPH::Vec3(0.0f, 0.0f, speed));
        for (int i = 0; i < 120; ++i)
        {
            step();
        }
        double lost = 0.0;
        const auto health = world.BondHealth(wall);
        const auto full = world.BondFullHealth(wall);
        for (std::size_t bond = 0; bond < health.size(); ++bond)
        {
            lost += static_cast<double>(full[bond] - health[bond]);
        }
        return lost;
    };

    const double slow = ram(4.0f);
    const double fast = ram(10.0f);
    EXPECT_GT(slow, 0.0);
    // The contact impulse is about mass x speed, and impact damage is proportional to the impulse above the
    // threshold: (1500 x 10 - 2000) / (1500 x 4 - 2000) = 3.25.
    const interi::destruction::DamageSettings damage;
    const double mass = static_cast<double>(OneGear().mass);
    const double threshold = static_cast<double>(damage.impactThreshold);
    const double expected = (mass * 10.0 - threshold) / (mass * 4.0 - threshold);
    EXPECT_NEAR(fast / slow, expected, 0.2 * expected);
}

TEST_F(VehicleTest, HardImpact_TakesDamage)
{
    scene.AddStaticBox(Vec3(0.0f, 1.5f, 3.0f), Vec3(3.0f, 1.5f, 0.5f));
    const VehicleDef def = OneGear();
    Vehicle vehicle(scene.physics, scene.world, def, At(Vec3(0.0f, 1.5f, -4.0f)));
    Steps(vehicle, 60);
    EXPECT_EQ(vehicle.Health(), def.health); // settling is not an impact
    scene.physics.Bodies().SetLinearVelocity(vehicle.BodyId(), JPH::Vec3(0.0f, 0.0f, 12.0f));
    Steps(vehicle, 90);
    EXPECT_LT(vehicle.Health(), def.health);
    EXPECT_FALSE(vehicle.IsDestroyed());
    EXPECT_EQ(scene.world.Get<VehicleComponent>(vehicle.GetEntity())->health, vehicle.Health());
}

TEST_F(VehicleTest, RadialDamage_FallsOffWithDistance)
{
    const VehicleDef def = OneGear();
    Vehicle vehicle(scene.physics, scene.world, def, At(Vec3(0.0f, 1.5f, 0.0f)));
    Steps(vehicle, 30);
    const Vec3 center = vehicle.Pose().position;
    vehicle.ApplyRadialDamage({.center = center, .energy = 4000.0f, .innerRadius = 1.0f, .radius = 6.0f});
    EXPECT_NEAR(vehicle.Health(), def.health - 4000.0f * def.explosionDamagePerJoule, 1e-3f);
    const float before = vehicle.Health();
    vehicle.ApplyRadialDamage(
        {.center = center + Vec3(2.0f, 0.0f, 0.0f), .energy = 4000.0f, .innerRadius = 1.0f, .radius = 6.0f});
    EXPECT_NEAR(before - vehicle.Health(), 0.25f * 4000.0f * def.explosionDamagePerJoule, 1e-2f);
    vehicle.ApplyRadialDamage({.center = center + Vec3(20.0f, 0.0f, 0.0f), .energy = 4000.0f, .radius = 6.0f});
    EXPECT_NEAR(before - vehicle.Health(), 0.25f * 4000.0f * def.explosionDamagePerJoule, 1e-2f);
}

TEST_F(VehicleTest, Destroyed_SpawnsWreckDebris)
{
    const VehicleDef def = OneGear();
    Vehicle vehicle(scene.physics, scene.world, def, At(Vec3(0.0f, 1.5f, -40.0f)));
    Steps(vehicle, 60);
    scene.physics.Bodies().SetLinearVelocity(vehicle.BodyId(), JPH::Vec3(0.0f, 0.0f, 5.0f));
    Step(vehicle);
    const Entity entity = vehicle.GetEntity();
    const JPH::BodyID body = vehicle.BodyId();
    const Vec3 chassis = vehicle.Pose().position;

    vehicle.ApplyDamage(def.health + 1.0f);
    EXPECT_FALSE(vehicle.IsDestroyed()); // applied at the next Update
    Step(vehicle);
    ASSERT_TRUE(vehicle.IsDestroyed());
    EXPECT_TRUE(vehicle.BodyId().IsInvalid());
    EXPECT_FALSE(scene.physics.Bodies().IsAdded(body));
    EXPECT_FALSE(scene.world.IsAlive(entity));

    ASSERT_EQ(vehicle.WreckEntities().size(), 5u); // chassis + 4 wheels
    float mass = 0.0f;
    for (std::size_t i = 0; i < vehicle.WreckEntities().size(); ++i)
    {
        const Entity piece = vehicle.WreckEntities()[i];
        const RigidBodyComponent* rigid = scene.world.Get<RigidBodyComponent>(piece);
        ASSERT_NE(rigid, nullptr);
        ASSERT_NE(scene.world.Get<interi::physics::ColliderComponent>(piece), nullptr);
        const WreckComponent* wreck = scene.world.Get<WreckComponent>(piece);
        ASSERT_NE(wreck, nullptr);
        EXPECT_EQ(wreck->wheel, i > 0);
        EXPECT_EQ(rigid->motion, MotionType::Dynamic);
        EXPECT_EQ(rigid->layer, ObjectLayer::Debris);
        EXPECT_NEAR(rigid->linearVelocity.z, 5.0f, 0.6f);
        mass += rigid->mass;
    }
    EXPECT_NEAR(mass, def.mass, 1e-2f);
    EXPECT_NEAR(scene.world.Get<Transform>(vehicle.WreckEntities()[0])->position.z, chassis.z, 0.2f);

    // The pieces become bodies and keep rolling on.
    Steps(vehicle, 30);
    for (const Entity piece : vehicle.WreckEntities())
    {
        EXPECT_NE(scene.world.Get<interi::physics::PhysicsBodyComponent>(piece), nullptr);
    }
    EXPECT_GT(scene.world.Get<Transform>(vehicle.WreckEntities()[0])->position.z, chassis.z + 1.0f);
}

TEST_F(VehicleTest, Destructor_RemovesBodyAndEntity)
{
    JPH::BodyID body;
    Entity entity;
    {
        Vehicle vehicle(scene.physics, scene.world, OneGear(), At(Vec3(0.0f, 1.5f, 0.0f)));
        Steps(vehicle, 5);
        body = vehicle.BodyId();
        entity = vehicle.GetEntity();
    }
    EXPECT_FALSE(scene.physics.Bodies().IsAdded(body));
    EXPECT_FALSE(scene.world.IsAlive(entity));
    scene.Step(); // the constraint is gone too: stepping must not touch the destroyed body
}

TEST(VehicleDeterminism, SameInput_SamePoseAtAnyThreadCount)
{
    const auto drive = [](std::uint32_t workers)
    {
        GameplayScene scene(workers);
        scene.AddGround();
        for (int i = 0; i < 8; ++i)
        {
            scene.AddDynamicBox(Vec3(-1.0f + 0.4f * static_cast<float>(i), 0.3f, 6.0f), Vec3(0.2f), 30.0f);
        }
        Vehicle vehicle(scene.physics, scene.world, OneGear(), At(Vec3(0.0f, 1.5f, 0.0f)));
        for (int i = 0; i < 240; ++i)
        {
            vehicle.SetInput({.forward = 1.0f, .right = i > 120 ? 0.5f : 0.0f});
            scene.Step();
            vehicle.Update();
        }
        return vehicle.Pose();
    };
    const Transform one = drive(1);
    for (const std::uint32_t workers : {4u, 8u})
    {
        const Transform other = drive(workers);
        EXPECT_EQ(one.position.x, other.position.x) << workers;
        EXPECT_EQ(one.position.z, other.position.z) << workers;
        EXPECT_EQ(one.rotation.y, other.rotation.y) << workers;
    }
}

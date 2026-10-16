// SPDX-License-Identifier: MIT
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/gameplay/Weapons.h>
#include <interi/testing/ExpectNear.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

using interi::Entity;
using interi::Transform;
using interi::Vec3;
using interi::destruction::DestructibleId;
using interi::destruction::DestructionWorld;
using interi::gameplay::WeaponDef;
using interi::gameplay::WeaponKind;
using interi::gameplay::WeaponSystem;
using interi::testing::GameplayScene;
using interi::testing::kGameplayStep;

namespace
{

WeaponDef Weapon(WeaponKind kind, float energy, float innerRadius, float radius, float impulse, float range)
{
    WeaponDef weapon;
    weapon.name = std::string(interi::gameplay::ToString(kind));
    weapon.kind = kind;
    weapon.energy = energy;
    weapon.innerRadius = innerRadius;
    weapon.radius = radius;
    weapon.impulse = impulse;
    weapon.range = range;
    return weapon;
}

WeaponDef Hammer()
{
    WeaponDef weapon = Weapon(WeaponKind::Melee, 400.0f, 0.3f, 0.8f, 600.0f, 2.0f);
    weapon.hitRadius = 0.1f;
    return weapon;
}

WeaponDef Charge()
{
    WeaponDef weapon = Weapon(WeaponKind::RemoteCharge, 20000.0f, 1.0f, 3.0f, 3000.0f, 4.0f);
    weapon.maxCharges = 2;
    return weapon;
}

WeaponDef RocketDef()
{
    WeaponDef weapon = Weapon(WeaponKind::Rocket, 8000.0f, 0.8f, 2.5f, 2000.0f, 30.0f);
    weapon.speed = 40.0f;
    weapon.hitRadius = 0.1f;
    return weapon;
}

interi::destruction::testing::GridAssetOptions Fragile()
{
    interi::destruction::testing::GridAssetOptions options;
    options.fractureEnergy = 100.0f;
    return options;
}

class WeaponsTest : public ::testing::Test
{
protected:
    WeaponsTest() { scene.AddGround(); }

    /// A wall of unit cubes from x in [0, 4), y in [0, 3), z in [0, 1), anchored at the ground row.
    DestructibleId SpawnWall(interi::destruction::testing::GridAssetOptions options = {})
    {
        auto asset = std::make_shared<const interi::destruction::DestructibleAsset>(
            interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(4, 3, 1), options));
        return destruction.Spawn(scene.world, scene.jobs, asset, Transform{});
    }

    /// The full fixed step: bodies, weapons, destruction.
    void Step()
    {
        scene.Step();
        weapons.Update(scene.world, kGameplayStep);
        destruction.Update(scene.world, scene.jobs);
    }

    [[nodiscard]] std::size_t BrokenBonds(DestructibleId id) const
    {
        std::size_t broken = 0;
        const std::size_t count = destruction.BondHealth(id).size();
        for (std::uint32_t bond = 0; bond < count; ++bond)
        {
            broken += destruction.IsBondBroken(id, bond) ? 1 : 0;
        }
        return broken;
    }

    GameplayScene scene;
    DestructionWorld destruction{scene.physics};
    WeaponSystem weapons{scene.physics, destruction};
};

} // namespace

TEST_F(WeaponsTest, Sledgehammer_Hit_AppliesImpulseAtContact)
{
    const float mass = 50.0f;
    const Entity box = scene.AddDynamicBox(Vec3(0.0f, 0.5f, 0.0f), Vec3(0.5f), mass);
    scene.Step(); // creates the body; it rests on the ground
    const WeaponDef hammer = Hammer();

    // Strike the box's -x face 0.3 m above its centre, along +x.
    const auto hit = weapons.Swing(hammer, Vec3(-1.5f, 0.8f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, box);
    EXPECT_NEAR(hit->point.x, -0.5f, 0.02f);
    EXPECT_NEAR(hit->normal.x, -1.0f, 0.01f);

    const JPH::BodyID body = scene.world.Get<interi::physics::PhysicsBodyComponent>(box)->id;
    const JPH::Vec3 linear = scene.physics.Bodies().GetLinearVelocity(body);
    const JPH::Vec3 angular = scene.physics.Bodies().GetAngularVelocity(body);
    EXPECT_NEAR(linear.GetX(), hammer.impulse / mass, 0.05f);
    EXPECT_NEAR(linear.GetY(), 0.0f, 0.01f);
    // Above the centre of mass: r x J points along -z, so the box tips forward.
    EXPECT_LT(angular.GetZ(), -0.5f);
    EXPECT_NEAR(angular.GetX(), 0.0f, 0.01f);
}

TEST_F(WeaponsTest, Sledgehammer_HitWall_DamagesBondsAtContact)
{
    const DestructibleId wall = SpawnWall(Fragile());
    Step();
    const WeaponDef hammer = Hammer();
    // Hit the front face (z = 1) of the cube at x in [1, 2), y in [1, 2), next to the bond at x = 2.
    const auto hit = weapons.Swing(hammer, Vec3(1.9f, 1.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->point.z, 1.0f, 0.02f);
    Step();
    EXPECT_GT(BrokenBonds(wall), 0u);
    EXPECT_LT(BrokenBonds(wall), destruction.BondHealth(wall).size()); // local damage, not the whole wall
}

TEST_F(WeaponsTest, Sledgehammer_Miss_NoEffect)
{
    const auto hit = weapons.Swing(Hammer(), Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(hit.has_value());
}

TEST_F(WeaponsTest, RemoteCharge_AttachToWall_DetonateDamagesBonds)
{
    const DestructibleId wall = SpawnWall(Fragile());
    Step();
    const WeaponDef charge = Charge();
    const auto id = weapons.PlaceCharge(scene.world, charge, Vec3(2.0f, 1.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(id.has_value());
    ASSERT_EQ(weapons.Charges().size(), 1u);
    EXPECT_EQ(weapons.Charges()[0].instance, wall);
    EXPECT_NEAR(weapons.Charges()[0].position.z, 1.0f, 0.02f);

    for (int i = 0; i < 10; ++i)
    {
        Step(); // placed charges do nothing
    }
    EXPECT_EQ(BrokenBonds(wall), 0u);

    weapons.DetonateCharges();
    Step();
    ASSERT_EQ(weapons.Blasts().size(), 1u);
    EXPECT_TRUE(weapons.Charges().empty());
    EXPECT_GT(BrokenBonds(wall), 0u);
}

TEST_F(WeaponsTest, RemoteCharge_OverMax_RemovesOldest)
{
    SpawnWall();
    Step();
    const WeaponDef charge = Charge(); // maxCharges 2
    const auto first = weapons.PlaceCharge(scene.world, charge, Vec3(0.5f, 0.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    const auto second = weapons.PlaceCharge(scene.world, charge, Vec3(1.5f, 0.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    const auto third = weapons.PlaceCharge(scene.world, charge, Vec3(2.5f, 0.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(first && second && third);
    ASSERT_EQ(weapons.Charges().size(), 2u);
    EXPECT_EQ(weapons.Charges()[0].id, *second);
    EXPECT_EQ(weapons.Charges()[1].id, *third);
}

TEST_F(WeaponsTest, RemoteCharge_FollowsMovingBody)
{
    const Entity box = scene.AddDynamicBox(Vec3(0.0f, 3.0f, 0.0f), Vec3(0.5f), 20.0f);
    scene.Step();
    const auto id = weapons.PlaceCharge(scene.world, Charge(), Vec3(0.0f, 3.0f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(weapons.Charges()[0].entity, box);
    const float startY = weapons.Charges()[0].position.y;
    for (int i = 0; i < 90; ++i)
    {
        Step(); // the box falls to the ground
    }
    const Vec3 boxPosition = scene.PositionOf(box);
    EXPECT_LT(boxPosition.y, 0.6f);
    EXPECT_NEAR(weapons.Charges()[0].position.y, startY - (3.0f - boxPosition.y), 0.05f);
    EXPECT_NEAR(weapons.Charges()[0].position.z, boxPosition.z + 0.5f, 0.05f);
}

TEST_F(WeaponsTest, RemoteCharge_OnFallingPiece_FollowsChunk)
{
    // An unanchored destructible is one dynamic piece: the charge follows its chunk as it falls.
    interi::destruction::testing::GridAssetOptions options;
    options.anchored = [](const interi::destruction::testing::Cell&) { return false; };
    auto asset = std::make_shared<const interi::destruction::DestructibleAsset>(
        interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(2, 1, 1), options));
    Transform pose;
    pose.position = Vec3(0.0f, 3.0f, 0.0f);
    const DestructibleId id = destruction.Spawn(scene.world, scene.jobs, asset, pose);
    Step();
    ASSERT_TRUE(weapons.PlaceCharge(scene.world, Charge(), Vec3(1.5f, 3.5f, 3.0f), Vec3(0.0f, 0.0f, -1.0f)));
    EXPECT_EQ(weapons.Charges()[0].instance, id);
    EXPECT_EQ(weapons.Charges()[0].chunk, 1u);
    for (int i = 0; i < 90; ++i)
    {
        Step();
    }
    const Transform piece = *destruction.ChunkPose(scene.world, id, 1);
    EXPECT_LT(piece.position.y, 0.5f);
    INTERI_EXPECT_VEC_NEAR(weapons.Charges()[0].localPoint, Vec3(1.5f, 0.5f, 1.0f), 0.01f); // asset space
    const Vec3 expected = piece.TransformPoint(weapons.Charges()[0].localPoint);
    INTERI_EXPECT_VEC_NEAR(weapons.Charges()[0].position, expected, 1e-3f);
}

TEST_F(WeaponsTest, Rocket_HitsWall_Explodes)
{
    const DestructibleId wall = SpawnWall(Fragile());
    Step();
    const WeaponDef rocket = RocketDef();
    weapons.FireRocket(rocket, Vec3(2.5f, 1.5f, 20.0f), Vec3(0.0f, 0.0f, -1.0f)); // a face centre, not an edge
    bool exploded = false;
    for (int i = 0; i < 60 && !exploded; ++i)
    {
        Step();
        if (!weapons.Blasts().empty())
        {
            exploded = true;
            EXPECT_NEAR(weapons.Blasts()[0].center.z, 1.05f, 0.15f); // on the front face
            EXPECT_NEAR(weapons.Blasts()[0].normal.z, 1.0f, 0.01f);
        }
    }
    ASSERT_TRUE(exploded);
    EXPECT_TRUE(weapons.Rockets().empty());
    EXPECT_GT(BrokenBonds(wall), 0u);
}

TEST_F(WeaponsTest, Rocket_MaxRange_ExplodesInAir)
{
    WeaponDef rocket = RocketDef();
    rocket.range = 5.0f;
    weapons.FireRocket(rocket, Vec3(0.0f, 10.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
    int steps = 0;
    while (weapons.Blasts().empty() && steps < 60)
    {
        Step();
        ++steps;
    }
    ASSERT_FALSE(weapons.Blasts().empty());
    EXPECT_NEAR(weapons.Blasts()[0].center.x, 5.0f, rocket.speed * kGameplayStep);
    EXPECT_EQ(steps, static_cast<int>(std::ceil(5.0f / (rocket.speed * kGameplayStep))));
}

TEST_F(WeaponsTest, Blast_PushesNearbyDebrisAway)
{
    const Entity near = scene.AddDynamicBox(Vec3(1.0f, 0.25f, 0.0f), Vec3(0.25f), 10.0f);
    const Entity far = scene.AddDynamicBox(Vec3(10.0f, 0.25f, 0.0f), Vec3(0.25f), 10.0f);
    Step();
    WeaponDef rocket = RocketDef();
    rocket.range = 0.01f; // blast right away, at the origin
    weapons.FireRocket(rocket, Vec3(0.0f, 0.25f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    Step();
    ASSERT_EQ(weapons.Blasts().size(), 1u);
    const auto velocity = [&](Entity entity)
    {
        return scene.physics.Bodies().GetLinearVelocity(
            scene.world.Get<interi::physics::PhysicsBodyComponent>(entity)->id);
    };
    EXPECT_GT(velocity(near).GetX(), 1.0f);
    EXPECT_NEAR(velocity(far).GetX(), 0.0f, 1e-3f);
}

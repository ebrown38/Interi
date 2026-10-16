// SPDX-License-Identifier: MIT
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/physics/Components.h>
#include <interi/physics/PhysicsSync.h>

#include "../physics/PhysicsTestUtil.h"
#include "RuntimeTestUtil.h"
#include <gtest/gtest.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
// clang-format on

#include <array>
#include <memory>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;
using namespace interi::physics;
using interi::physics::testing::AddGround;
using interi::physics::testing::Workers;

constexpr float kStep = 1.0f / 60.0f;

class DebrisSystemTest : public ::testing::Test
{
protected:
    explicit DebrisSystemTest(const DebrisSettings& settings = {})
        : m_jobs(Workers(4))
        , m_physics(m_jobs)
        , m_sync(m_physics)
        , m_debris(m_physics, settings)
    {
        AddGround(m_physics);
    }

    /// A dynamic box of half extent `half` at `position`.
    Entity Box(const Vec3& position, float half, bool asleep = false)
    {
        Transform pose;
        pose.position = position;
        RigidBodyComponent body;
        body.layer = ObjectLayer::DebrisSmall;
        body.startAsleep = asleep;
        return m_world.CreateEntity(pose, body, ColliderComponent::Box(Vec3(half)));
    }

    bool AdoptBox(const Vec3& position, float half, Entity* out = nullptr)
    {
        const Entity entity = Box(position, half);
        if (out != nullptr)
        {
            *out = entity;
        }
        return m_debris.Adopt(m_world, entity, 8.0f * half * half * half);
    }

    void Step(const DebrisView& view = {})
    {
        m_sync.Step(m_world, m_jobs, kStep);
        m_debris.Update(m_world, m_jobs, view, kStep);
    }

    JobSystem m_jobs;
    PhysicsWorld m_physics;
    PhysicsSync m_sync;
    World m_world;
    DebrisSystem m_debris;
};

TEST_F(DebrisSystemTest, Adopt_AddsDebrisComponentWithBounds)
{
    Entity entity;
    ASSERT_TRUE(AdoptBox(Vec3(1.0f, 2.0f, 3.0f), 0.25f, &entity));
    const auto* debris = m_world.Get<DebrisComponent>(entity);
    ASSERT_NE(debris, nullptr);
    EXPECT_EQ(debris->state, DebrisState::Body);
    EXPECT_NEAR(debris->volume, 0.125f, 1e-6f);
    EXPECT_NEAR(Length(debris->localCenter), 0.0f, 1e-5f);
    EXPECT_GE(debris->radius, 0.25f * 1.7f);
    EXPECT_EQ(m_debris.BodyCount(), 1u);
}

class DebrisCapTest : public DebrisSystemTest
{
protected:
    DebrisCapTest()
        : DebrisSystemTest(Settings())
    {
    }

    static DebrisSettings Settings()
    {
        DebrisSettings settings;
        settings.maxBodies = 2;
        return settings;
    }
};

TEST_F(DebrisCapTest, AtCap_EvictsSmallestBody)
{
    Entity small;
    Entity medium;
    Entity large;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 1.0f, 0.0f), 0.1f, &small));
    ASSERT_TRUE(AdoptBox(Vec3(2.0f, 1.0f, 0.0f), 0.2f, &medium));
    ASSERT_TRUE(AdoptBox(Vec3(4.0f, 1.0f, 0.0f), 0.3f, &large));
    EXPECT_FALSE(m_world.IsAlive(small));
    EXPECT_TRUE(m_world.IsAlive(medium));
    EXPECT_TRUE(m_world.IsAlive(large));
    EXPECT_EQ(m_debris.BodyCount(), 2u);

    Step();
    EXPECT_EQ(m_sync.BodyCount(), 2u);
    EXPECT_EQ(m_debris.LastStats().adopted, 3u);
    EXPECT_EQ(m_debris.LastStats().evicted, 1u);
}

TEST_F(DebrisCapTest, AtCap_LowerPriorityNewcomer_Destroyed)
{
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 1.0f, 0.0f), 0.3f));
    ASSERT_TRUE(AdoptBox(Vec3(2.0f, 1.0f, 0.0f), 0.3f));
    Entity tiny;
    EXPECT_FALSE(AdoptBox(Vec3(4.0f, 1.0f, 0.0f), 0.05f, &tiny));
    EXPECT_FALSE(m_world.IsAlive(tiny));
    EXPECT_EQ(m_debris.BodyCount(), 2u);
}

TEST_F(DebrisCapTest, DestroyedElsewhere_SlotFreedAtUpdate)
{
    Entity a;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 1.0f, 0.0f), 0.3f, &a));
    ASSERT_TRUE(AdoptBox(Vec3(2.0f, 1.0f, 0.0f), 0.3f));
    m_world.DestroyEntity(a);
    Step();
    EXPECT_EQ(m_debris.BodyCount(), 1u);
    EXPECT_TRUE(AdoptBox(Vec3(4.0f, 1.0f, 0.0f), 0.05f));
    EXPECT_EQ(m_debris.LastStats().evicted, 0u);
}

TEST_F(DebrisSystemTest, CullPlanes_SphereOutside_NotVisible)
{
    Entity inside;
    Entity outside;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 5.0f, 5.0f), 0.2f, &inside));
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 5.0f, -5.0f), 0.2f, &outside));
    const std::array<Vec4, 1> planes{Vec4(0.0f, 0.0f, 1.0f, 0.0f)};
    Step({.position = {}, .cullPlanes = planes});
    EXPECT_TRUE(m_world.Get<DebrisComponent>(inside)->visible);
    EXPECT_FALSE(m_world.Get<DebrisComponent>(outside)->visible);
}

// --- LOD, fade, rubble ---

TEST_F(DebrisSystemTest, DebrisLOD_BeyondDistance_ConvertsToParticle)
{
    const DebrisPolicy& policy = m_debris.Settings().policy;
    Entity entity;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 5.0f, 0.0f), 0.1f, &entity)); // 0.008 m^3 <= particleMaxVolume
    Entity large;
    ASSERT_TRUE(AdoptBox(Vec3(3.0f, 5.0f, 0.0f), 0.5f, &large));
    const DebrisView far{.position = Vec3(0.0f, 5.0f, policy.particleDistance + 10.0f), .cullPlanes = {}};
    Step(far);

    EXPECT_EQ(m_debris.LastStats().toParticles, 1u);
    EXPECT_EQ(m_debris.ParticleCount(), 1u);
    EXPECT_EQ(m_debris.BodyCount(), 1u);
    EXPECT_FALSE(m_world.Has<RigidBodyComponent>(entity));
    ASSERT_TRUE(m_world.Has<DebrisParticleComponent>(entity));
    EXPECT_EQ(m_world.Get<DebrisComponent>(entity)->state, DebrisState::Particle);
    EXPECT_TRUE(m_world.Has<RigidBodyComponent>(large));

    // The body is gone at the next sync; the particle keeps falling (it no longer collides).
    const float y0 = m_world.Get<Transform>(entity)->position.y;
    for (int i = 0; i < 60; ++i)
    {
        Step(far);
    }
    EXPECT_EQ(m_sync.BodyCount(), 1u);
    EXPECT_LT(m_world.Get<Transform>(entity)->position.y, y0 - 3.0f);
    EXPECT_LT(m_world.Get<DebrisParticleComponent>(entity)->linearVelocity.y, -9.0f);
}

TEST_F(DebrisSystemTest, DebrisLOD_WithinDistance_StaysBody)
{
    Entity entity;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 5.0f, 0.0f), 0.1f, &entity));
    Step({.position = Vec3(0.0f, 5.0f, 10.0f), .cullPlanes = {}});
    EXPECT_TRUE(m_world.Has<RigidBodyComponent>(entity));
    EXPECT_EQ(m_debris.ParticleCount(), 0u);
}

class DebrisFadeTest : public DebrisSystemTest
{
protected:
    DebrisFadeTest()
        : DebrisSystemTest(Settings())
    {
    }

    static DebrisSettings Settings()
    {
        DebrisSettings settings;
        settings.policy.fadeMaxVolume = 0.01f;
        settings.policy.fadeDelay = 0.5f;
        settings.policy.fadeDuration = 0.5f;
        settings.policy.particleLifetime = 1.0f;
        settings.mergeRubble = false;
        return settings;
    }
};

TEST_F(DebrisFadeTest, DebrisFade_SmallPieceTimeout_Removed)
{
    Entity small;
    Entity large;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 0.1f, 0.0f), 0.1f, &small)); // 0.008 m^3: fades
    ASSERT_TRUE(AdoptBox(Vec3(2.0f, 0.5f, 0.0f), 0.5f, &large));
    for (int i = 0; i < 45; ++i) // 0.75 s: half faded
    {
        Step();
    }
    ASSERT_TRUE(m_world.IsAlive(small));
    EXPECT_GT(m_world.Get<DebrisComponent>(small)->opacity, 0.0f);
    EXPECT_LT(m_world.Get<DebrisComponent>(small)->opacity, 1.0f);
    std::uint32_t removed = 0;
    for (int i = 0; i < 20; ++i)
    {
        Step();
        removed += m_debris.LastStats().removed;
    }
    EXPECT_FALSE(m_world.IsAlive(small));
    EXPECT_EQ(removed, 1u);
    EXPECT_TRUE(m_world.IsAlive(large));
    EXPECT_EQ(m_world.Get<DebrisComponent>(large)->opacity, 1.0f);
    EXPECT_EQ(m_debris.BodyCount(), 1u);
}

TEST_F(DebrisFadeTest, Particle_AfterLifetime_Removed)
{
    Entity entity;
    ASSERT_TRUE(AdoptBox(Vec3(0.0f, 50.0f, 0.0f), 0.13f, &entity)); // 0.0176 m^3: a particle, no body fade
    const DebrisView far{.position = Vec3(0.0f, 50.0f, 500.0f), .cullPlanes = {}};
    Step(far);
    ASSERT_TRUE(m_world.Has<DebrisParticleComponent>(entity));
    for (int i = 0; i < 55; ++i)
    {
        Step(far);
    }
    EXPECT_TRUE(m_world.IsAlive(entity));
    for (int i = 0; i < 10; ++i)
    {
        Step(far);
    }
    EXPECT_FALSE(m_world.IsAlive(entity));
    EXPECT_EQ(m_debris.ParticleCount(), 0u);
}

TEST_F(DebrisSystemTest, RubbleMerge_SleepingCluster_SingleStaticBody)
{
    // Four boxes resting on the ground within one 2 m cell, and one far away.
    std::vector<Entity> cluster;
    for (int i = 0; i < 4; ++i)
    {
        Entity entity;
        ASSERT_TRUE(AdoptBox(Vec3(0.3f + 0.45f * static_cast<float>(i), 0.2f, 0.5f), 0.2f, &entity));
        cluster.push_back(entity);
    }
    Entity loner;
    ASSERT_TRUE(AdoptBox(Vec3(20.5f, 0.2f, 0.5f), 0.2f, &loner));

    std::uint32_t piles = 0;
    for (int i = 0; i < 300 && piles == 0; ++i)
    {
        Step();
        piles += m_debris.LastStats().rubblePiles;
    }
    ASSERT_EQ(piles, 1u);
    EXPECT_EQ(m_debris.LastStats().rubblePieces, 4u);
    for (const Entity entity : cluster)
    {
        EXPECT_FALSE(m_world.IsAlive(entity));
    }
    EXPECT_TRUE(m_world.IsAlive(loner));
    EXPECT_EQ(m_debris.BodyCount(), 1u);

    Entity rubble;
    std::uint32_t rubbleCount = 0;
    Query query;
    query.With<RubbleComponent>();
    query.ForEachChunk(m_world,
                       [&](ChunkView& chunk)
                       {
                           rubbleCount += chunk.Count();
                           rubble = chunk.Entities()[0];
                       });
    ASSERT_EQ(rubbleCount, 1u);
    EXPECT_EQ(m_world.Get<RubbleComponent>(rubble)->pieces, 4u);
    EXPECT_EQ(m_world.Get<RigidBodyComponent>(rubble)->motion, MotionType::Static);

    Step();
    EXPECT_EQ(m_sync.BodyCount(), 2u); // the pile and the loner
    // The pile collides like the pieces did: a ray down onto it hits its top (y = 0.4).
    JPH::RRayCast ray{JPH::RVec3(0.3f, 5.0f, 0.5f), JPH::Vec3(0.0f, -10.0f, 0.0f)};
    JPH::RayCastResult hit;
    ASSERT_TRUE(m_physics.System().GetNarrowPhaseQuery().CastRay(ray, hit));
    EXPECT_NEAR(5.0f - hit.mFraction * 10.0f, 0.4f, 0.02f);
}

TEST_F(DebrisSystemTest, RubbleMerge_TooFewPieces_StayBodies)
{
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(AdoptBox(Vec3(0.3f + 0.45f * static_cast<float>(i), 0.2f, 0.5f), 0.2f));
    }
    for (int i = 0; i < 200; ++i)
    {
        Step();
    }
    EXPECT_EQ(m_debris.BodyCount(), 3u);
}

// --- Handoff from DestructionWorld ---

class DebrisHandoffTest : public DebrisSystemTest
{
protected:
    DebrisHandoffTest()
        : m_destruction(m_physics, Settings())
    {
        m_destruction.SetDebrisSystem(&m_debris);
    }

    static DestructionSettings Settings()
    {
        DestructionSettings settings;
        settings.smallPieceVolume = 2.5f; // two unit cubes count as small
        settings.explosionPushSpeed = 0.0f;
        return settings;
    }

    void StepAll()
    {
        m_sync.Step(m_world, m_jobs, kStep);
        m_destruction.Update(m_world, m_jobs);
        m_debris.Update(m_world, m_jobs, {}, kStep);
    }

    DestructionWorld m_destruction;
};

TEST_F(DebrisHandoffTest, SmallPiece_HandedToDebrisAndLeavesInstance)
{
    // A column of four cubes anchored at the bottom; breaking between y=1 and y=2 frees {2, 3} (2 m^3).
    DestructibleAsset asset = MakeGridAsset({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}});
    const std::uint32_t cut = *FindBond(asset, 1, 2);
    const std::uint32_t inner = *FindBond(asset, 2, 3);
    const DestructibleId id =
        m_destruction.Spawn(m_world, m_jobs, std::make_shared<const DestructibleAsset>(std::move(asset)), {});
    m_destruction.QueueDamage({.center = Vec3(0.5f, 2.0f, 0.5f), .energy = 1.0e9f, .innerRadius = 0.4f, .radius = 0.4f},
                              DamageKind::Explosion);
    StepAll();

    EXPECT_TRUE(m_destruction.IsBondBroken(id, cut));
    EXPECT_TRUE(m_destruction.IsBondBroken(id, inner)); // gone with the piece
    EXPECT_EQ(m_destruction.LastStats().bondsBrokenByDamage, 1u);
    EXPECT_EQ(m_destruction.LastStats().piecesToDebris, 1u);
    EXPECT_EQ(m_debris.BodyCount(), 1u);

    std::uint32_t livePieces = 0;
    for (std::uint32_t piece = 0; piece < m_destruction.PieceCount(id); ++piece)
    {
        if (!m_destruction.PieceChunks(id, piece).empty())
        {
            ++livePieces;
            EXPECT_TRUE(m_destruction.PieceAnchored(id, piece));
        }
    }
    EXPECT_EQ(livePieces, 1u);

    Entity debrisEntity;
    std::uint32_t debrisCount = 0;
    Query query;
    query.With<DebrisComponent>();
    query.ForEachChunk(m_world,
                       [&](ChunkView& chunk)
                       {
                           debrisCount += chunk.Count();
                           debrisEntity = chunk.Entities()[0];
                       });
    ASSERT_EQ(debrisCount, 1u);
    EXPECT_FALSE(m_world.Has<DestructibleComponent>(debrisEntity));
    EXPECT_NEAR(m_world.Get<DebrisComponent>(debrisEntity)->volume, 2.0f, 1e-5f);

    // Released chunks take no more damage and form no pieces; Despawn leaves the debris.
    for (int i = 0; i < 10; ++i)
    {
        StepAll();
    }
    EXPECT_EQ(m_destruction.LastStats().piecesSpawned, 0u);
    EXPECT_TRUE(m_destruction.Despawn(m_world, id));
    EXPECT_TRUE(m_world.IsAlive(debrisEntity));
}

TEST_F(DebrisHandoffTest, LargePiece_StaysWithInstance)
{
    DestructibleAsset asset = MakeGridAsset({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 4, 0}});
    const DestructibleId id =
        m_destruction.Spawn(m_world, m_jobs, std::make_shared<const DestructibleAsset>(std::move(asset)), {});
    m_destruction.QueueDamage({.center = Vec3(0.5f, 2.0f, 0.5f), .energy = 1.0e9f, .innerRadius = 0.4f, .radius = 0.4f},
                              DamageKind::Explosion);
    StepAll(); // frees {2, 3, 4}: 3 m^3, not small
    EXPECT_EQ(m_destruction.LastStats().piecesToDebris, 0u);
    EXPECT_EQ(m_destruction.LastStats().piecesSpawned, 1u);
    EXPECT_EQ(m_debris.BodyCount(), 0u);
    (void)id;
}

} // namespace

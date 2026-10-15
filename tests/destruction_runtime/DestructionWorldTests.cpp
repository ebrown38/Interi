// SPDX-License-Identifier: MIT
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/Queries.h>
#include <interi/platform/FileSystem.h>

#include "../physics/PhysicsTestUtil.h"
#include "RuntimeTestUtil.h"
#include <gtest/gtest.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
// clang-format on

#include <algorithm>
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

void ExpectVecNear(const Vec3& actual, const Vec3& expected, float tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

/// Damage that breaks exactly the bonds whose centroid is within 0.4 m of `point`.
RadialDamage BreakAt(const Vec3& point)
{
    return {.center = point, .energy = 1.0e9f, .innerRadius = 0.4f, .radius = 0.4f};
}

class DestructionWorldTest : public ::testing::Test
{
protected:
    explicit DestructionWorldTest(const DestructionSettings& settings = {})
        : m_jobs(Workers(4))
        , m_physics(m_jobs)
        , m_sync(m_physics)
        , m_destruction(m_physics, settings)
    {
        AddGround(m_physics);
    }

    DestructibleId Spawn(DestructibleAsset asset, const Vec3& position = {})
    {
        Transform transform;
        transform.position = position;
        return m_destruction.Spawn(m_world, m_jobs, std::make_shared<const DestructibleAsset>(std::move(asset)),
                                   transform);
    }

    void Step()
    {
        m_sync.Step(m_world, m_jobs, kStep);
        m_destruction.Update(m_world, m_jobs);
    }

    /// Alive pieces (with chunks) of an instance.
    std::vector<std::uint32_t> LivePieces(DestructibleId id) const
    {
        std::vector<std::uint32_t> pieces;
        for (std::uint32_t piece = 0; piece < m_destruction.PieceCount(id); ++piece)
        {
            if (!m_destruction.PieceChunks(id, piece).empty())
            {
                pieces.push_back(piece);
            }
        }
        return pieces;
    }

    std::vector<std::uint32_t> Chunks(DestructibleId id, std::uint32_t piece) const
    {
        const auto chunks = m_destruction.PieceChunks(id, piece);
        return {chunks.begin(), chunks.end()};
    }

    const RigidBodyComponent& Body(DestructibleId id, std::uint32_t piece) const
    {
        return *m_world.Get<RigidBodyComponent>(m_destruction.PieceEntity(id, piece));
    }

    JobSystem m_jobs;
    PhysicsWorld m_physics;
    PhysicsSync m_sync;
    World m_world;
    DestructionWorld m_destruction;
};

class DestructionNoSeparationTest : public DestructionWorldTest
{
protected:
    DestructionNoSeparationTest()
        : DestructionWorldTest(NoSeparation())
    {
    }

    static DestructionSettings NoSeparation()
    {
        DestructionSettings settings;
        settings.separationSpeed = 0.0f;
        return settings;
    }
};

GridAssetOptions Unanchored()
{
    GridAssetOptions options;
    options.anchored = [](const Cell&) { return false; };
    return options;
}

} // namespace

TEST_F(DestructionWorldTest, DestructibleInstance_Spawn_SingleCompoundBody)
{
    const DestructibleId id = Spawn(MakeGridAsset(BoxCells(3, 3, 1)));
    ASSERT_TRUE(m_destruction.IsAlive(id));
    ASSERT_EQ(LivePieces(id).size(), 1u);
    EXPECT_TRUE(m_destruction.PieceAnchored(id, 0));
    EXPECT_EQ(Chunks(id, 0).size(), 9u);

    m_sync.CreateBodies(m_world);
    ASSERT_EQ(m_sync.BodyCount(), 1u);
    const JPH::BodyID body = m_world.Get<PhysicsBodyComponent>(m_destruction.PieceEntity(id, 0))->id;
    EXPECT_EQ(m_physics.Bodies().GetMotionType(body), JPH::EMotionType::Static);
    EXPECT_EQ(m_physics.Bodies().GetObjectLayer(body), ToJolt(ObjectLayer::Structure));
    const JPH::RefConst<JPH::Shape> shape = m_physics.Bodies().GetShape(body);
    ASSERT_EQ(shape->GetSubType(), JPH::EShapeSubType::StaticCompound);
    EXPECT_EQ(static_cast<const JPH::StaticCompoundShape*>(shape.GetPtr())->GetNumSubShapes(), 9u);
}

TEST_F(DestructionWorldTest, DestructibleInstance_Despawn_FreesAllChunks)
{
    const DestructibleId standing = Spawn(MakeGridAsset(BoxCells(2, 2, 1)));
    const DestructibleId loose = Spawn(MakeGridAsset(BoxCells(2, 1, 1), Unanchored()), Vec3(5.0f, 3.0f, 0.0f));
    m_sync.CreateBodies(m_world);
    EXPECT_EQ(m_sync.BodyCount(), 2u);
    EXPECT_EQ(m_destruction.InstanceCount(), 2u);

    EXPECT_TRUE(m_destruction.Despawn(m_world, standing));
    EXPECT_TRUE(m_destruction.Despawn(m_world, loose));
    EXPECT_FALSE(m_destruction.Despawn(m_world, loose));
    m_sync.DestroyRemovedBodies(m_world);
    EXPECT_EQ(m_sync.BodyCount(), 0u);
    EXPECT_EQ(m_world.EntityCount(), 0u);
    EXPECT_EQ(m_destruction.InstanceCount(), 0u);
    EXPECT_FALSE(m_destruction.IsAlive(standing));

    // The slot is reused with a new generation: old ids stay dead.
    const DestructibleId again = Spawn(MakeGridAsset(BoxCells(1, 1, 1)));
    EXPECT_FALSE(m_destruction.IsAlive(standing) && m_destruction.IsAlive(loose));
    EXPECT_TRUE(m_destruction.IsAlive(again));
}

TEST_F(DestructionWorldTest, Spawn_Unanchored_OneDynamicPiece)
{
    const DestructibleId id = Spawn(MakeGridAsset(BoxCells(2, 2, 2), Unanchored()), Vec3(0.0f, 4.0f, 0.0f));
    ASSERT_EQ(LivePieces(id).size(), 1u);
    EXPECT_FALSE(m_destruction.PieceAnchored(id, 0));
    EXPECT_EQ(Body(id, 0).motion, MotionType::Dynamic);
    EXPECT_EQ(Body(id, 0).layer, ObjectLayer::Debris);
}

TEST_F(DestructionWorldTest, Separation_MassMatchesChunkSum)
{
    DestructibleAsset asset = MakeGridAsset({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, Unanchored());
    asset.chunks[0].mass = 100.0f;
    asset.chunks[1].mass = 200.0f;
    asset.chunks[2].mass = 300.0f;
    const DestructibleId id = Spawn(std::move(asset), Vec3(0.0f, 5.0f, 0.0f));
    m_sync.CreateBodies(m_world);
    const JPH::BodyID body = m_world.Get<PhysicsBodyComponent>(m_destruction.PieceEntity(id, 0))->id;
    const JPH::BodyLockRead lock(m_physics.System().GetBodyLockInterface(), body);
    ASSERT_TRUE(lock.Succeeded());
    EXPECT_NEAR(1.0f / lock.GetBody().GetMotionProperties()->GetInverseMass(), 600.0f, 0.1f);
}

TEST_F(DestructionNoSeparationTest, Separation_InheritsParentVelocity)
{
    const DestructibleId id = Spawn(MakeGridAsset({{0, 0, 0}, {1, 0, 0}}, Unanchored()), Vec3(0.0f, 10.0f, 0.0f));
    m_sync.CreateBodies(m_world);
    const JPH::BodyID body = m_world.Get<PhysicsBodyComponent>(m_destruction.PieceEntity(id, 0))->id;
    m_physics.Bodies().SetLinearVelocity(body, JPH::Vec3(3.0f, 0.0f, 0.0f));
    m_physics.Bodies().SetAngularVelocity(body, JPH::Vec3(0.0f, 0.0f, 1.0f));

    m_destruction.QueueDamage(BreakAt(Vec3(1.0f, 10.5f, 0.5f)), DamageKind::Explosion);
    m_destruction.Update(m_world, m_jobs);
    EXPECT_EQ(m_destruction.LastStats().bondsBrokenByDamage, 1u);
    const std::vector<std::uint32_t> pieces = LivePieces(id);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_FALSE(m_world.IsAlive(m_destruction.PieceEntity(id, 0))) << "the parent body is replaced";
    // v + w x r with r = -/+0.5 m along x from the parent's centre of mass.
    EXPECT_EQ(Chunks(id, pieces[0]), std::vector<std::uint32_t>{0});
    ExpectVecNear(Body(id, pieces[0]).linearVelocity, Vec3(3.0f, -0.5f, 0.0f), 1e-4f);
    ExpectVecNear(Body(id, pieces[1]).linearVelocity, Vec3(3.0f, 0.5f, 0.0f), 1e-4f);
    ExpectVecNear(Body(id, pieces[0]).angularVelocity, Vec3(0.0f, 0.0f, 1.0f), 1e-5f);
    EXPECT_NEAR(Body(id, pieces[0]).mass, 1000.0f, 1e-3f);
}

TEST_F(DestructionWorldTest, Explosion_DetachesUnsupportedPart)
{
    const DestructibleId id = Spawn(MakeGridAsset({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}}));
    m_destruction.QueueDamage(BreakAt(Vec3(0.5f, 1.0f, 0.5f)), DamageKind::Explosion);
    Step();
    EXPECT_EQ(m_destruction.LastStats().bondsBrokenByDamage, 1u);
    EXPECT_EQ(m_destruction.LastStats().piecesSpawned, 1u);
    EXPECT_TRUE(m_destruction.IsBondBroken(id, *FindBond(m_destruction.Asset(id), 0, 1)));
    const std::vector<std::uint32_t> pieces = LivePieces(id);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_TRUE(m_destruction.PieceAnchored(id, pieces[0]));
    EXPECT_EQ(Chunks(id, pieces[0]), std::vector<std::uint32_t>{0});
    EXPECT_FALSE(m_destruction.PieceAnchored(id, pieces[1]));
    EXPECT_EQ(Chunks(id, pieces[1]), (std::vector<std::uint32_t>{1, 2}));
    EXPECT_EQ(Body(id, pieces[1]).motion, MotionType::Dynamic);
    // Pushed away from the break, upwards.
    ExpectVecNear(Body(id, pieces[1]).linearVelocity, Vec3(0.0f, 0.5f, 0.0f), 1e-5f);
}

TEST_F(DestructionWorldTest, MeleeScale_AppliesToMeleeOnly)
{
    DestructionSettings settings;
    settings.damage.meleeScale = 0.0f;
    DestructionWorld destruction(m_physics, settings);
    const DestructibleId id = destruction.Spawn(
        m_world, m_jobs, std::make_shared<const DestructibleAsset>(MakeGridAsset({{0, 0, 0}, {0, 1, 0}})), {});
    destruction.QueueDamage(BreakAt(Vec3(0.5f, 1.0f, 0.5f)), DamageKind::Melee);
    destruction.Update(m_world, m_jobs);
    EXPECT_FALSE(destruction.IsBondBroken(id, 0));
    destruction.QueueDamage(BreakAt(Vec3(0.5f, 1.0f, 0.5f)), DamageKind::Explosion);
    destruction.Update(m_world, m_jobs);
    EXPECT_TRUE(destruction.IsBondBroken(id, 0));
}

TEST_F(DestructionWorldTest, Stress_OverloadedCantilever_BreaksAndFalls)
{
    // Root bending load 108 mg; a limit of 108 mg / 1.3 fails only the root (the next face carries 75 mg).
    std::vector<Cell> cells;
    for (int x = 0; x <= 6; ++x)
    {
        cells.push_back({x, 1, 0});
    }
    GridAssetOptions options;
    options.anchored = [](const Cell& cell) { return cell[0] == 0; };
    options.limit = 108.0f * 1000.0f * 9.81f / 1.3f;
    const DestructibleId id = Spawn(MakeGridAsset(cells, options));
    m_destruction.Update(m_world, m_jobs);
    EXPECT_EQ(m_destruction.LastStats().bondsBrokenByStress, 1u);
    EXPECT_TRUE(m_destruction.IsBondBroken(id, *FindBond(m_destruction.Asset(id), 0, 1)));
    const std::vector<std::uint32_t> pieces = LivePieces(id);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(Chunks(id, pieces[0]), std::vector<std::uint32_t>{0});
    EXPECT_EQ(Chunks(id, pieces[1]).size(), 6u);

    // Nothing else breaks while the arm falls clear.
    m_destruction.Update(m_world, m_jobs);
    EXPECT_EQ(m_destruction.LastStats().bondsBrokenByStress, 0u);
}

TEST_F(DestructionWorldTest, ChunkAt_RaycastHitsChunk)
{
    const DestructibleId id = Spawn(MakeGridAsset(BoxCells(3, 1, 1)));
    Step();
    const std::optional<QueryHit> hit = Raycast(m_physics, Vec3(2.5f, 5.0f, 0.5f), Vec3(0.0f, -10.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, m_destruction.PieceEntity(id, 0));
    EXPECT_EQ(m_destruction.ChunkAt(m_world, hit->entity, hit->subShape), std::optional<std::uint32_t>(2u));
    EXPECT_EQ(m_destruction.ChunkAt(m_world, Entity{}, 0), std::nullopt);
}

TEST(DestructionWorldGrace, FreshPiece_DoesNotDamageParent)
{
    // Any contact damages (negative threshold), so only the grace period protects the faces that just broke apart.
    const auto run = [](std::uint32_t graceSteps)
    {
        DestructionSettings settings;
        settings.separationSpeed = 0.0f;
        settings.spawnGraceSteps = graceSteps;
        settings.damage.impactThreshold = -1.0f;
        settings.damage.impactRadius = 2.0f; // reach the bond above the contact (1 m away)
        JobSystem jobs(Workers(2));
        PhysicsWorld physics(jobs);
        PhysicsSync sync(physics);
        World world;
        DestructionWorld destruction(physics, settings);
        AddGround(physics);
        const DestructibleId id = destruction.Spawn(
            world, jobs, std::make_shared<const DestructibleAsset>(MakeGridAsset({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}})),
            {});
        destruction.QueueDamage(BreakAt(Vec3(0.5f, 1.0f, 0.5f)), DamageKind::Explosion);
        destruction.Update(world, jobs);
        for (int step = 0; step < 10; ++step)
        {
            sync.Step(world, jobs, kStep);
            destruction.Update(world, jobs);
        }
        const auto health = destruction.BondHealth(id);
        return health[*FindBond(destruction.Asset(id), 1, 2)];
    };
    EXPECT_EQ(run(3), 100.0f);
    EXPECT_LT(run(0), 100.0f) << "control: without the grace period the touching faces damage each other";
}

TEST(DestructibleAssetLoading, ThroughFileSystem_RoundTrips)
{
    const DestructibleAsset asset = MakeGridAsset(BoxCells(2, 2, 1));
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("assets/wall.idest", WriteDestructibleAsset(asset, 42)));
    const Result<std::shared_ptr<const DestructibleAsset>> loaded = LoadDestructibleAsset(files, "assets/wall.idest");
    ASSERT_TRUE(loaded);
    EXPECT_EQ(**loaded, asset);
    EXPECT_FALSE(LoadDestructibleAsset(files, "assets/missing.idest"));
}

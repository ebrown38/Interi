// SPDX-License-Identifier: MIT
// ChunkMeshCache and ChunkGatherer on a real device (the cache uploads into a MeshPool) and real destruction.
#include <interi/destruction_render/BreakEffects.h>
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_render/CrackDecals.h>
#include <interi/render/MeshPool.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include "DestructionRenderTestUtil.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using namespace interi;
using namespace interi::destruction;
using interi::destruction::testing::DestructionScene;
using interi::destruction::testing::MakeFracturedWall;
using interi::testing::VulkanTest;

namespace
{

class ChunkGathererTest : public VulkanTest
{
protected:
    void SetUp() override
    {
        VulkanTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        resources = std::make_unique<rhi::ResourceManager>(*device);
        uploads = std::make_unique<rhi::UploadQueue>(*device, 4 * 1024 * 1024);
        auto pool = render::MeshPool::Create(*device, *resources, 256 * 1024, 1024 * 1024);
        ASSERT_TRUE(pool.has_value());
        meshes = std::move(*pool);
        cache = std::make_unique<ChunkMeshCache>(*meshes);
        auto commandPool = device->CreateCommandPool();
        ASSERT_TRUE(commandPool.has_value());
        commands = *commandPool;
    }

    void TearDown() override
    {
        cache.reset();
        uploads.reset();
        meshes.reset();
        if (commands.IsValid())
        {
            device->DestroyCommandPool(commands);
        }
        resources.reset();
        VulkanTest::TearDown();
    }

    /// Uploads the asset's meshes and waits for the copies.
    void Cache(const std::shared_ptr<const DestructibleAsset>& asset)
    {
        rhi::ICommandList& list = device->BeginCommandList(commands);
        ASSERT_TRUE(cache->Add(*uploads, list, asset).has_value());
        rhi::ICommandList* const lists[] = {&list};
        const std::uint64_t value = device->Submit(lists);
        uploads->Submitted(value);
        device->WaitForValue(value);
        device->ResetCommandPool(commands);
    }

    std::unique_ptr<rhi::ResourceManager> resources;
    std::unique_ptr<rhi::UploadQueue> uploads;
    std::unique_ptr<render::MeshPool> meshes;
    std::unique_ptr<ChunkMeshCache> cache;
    rhi::CommandPoolHandle commands;
    ChunkMaterials materials;
    ChunkGatherer gatherer;
    std::vector<render::RenderObject> objects;
};

} // namespace

TEST_F(ChunkGathererTest, Cache_UploadsEveryChunkOnce)
{
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    Cache(wall);
    Cache(wall);
    EXPECT_EQ(cache->AssetCount(), 1u);
    AssetMeshes* found = cache->Find(wall.get());
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(found->firstSubmesh.size(), wall->chunks.size() + 1);
    for (std::uint32_t c = 0; c < wall->chunks.size(); ++c)
    {
        ASSERT_EQ(found->Submeshes(c).size(), wall->chunks[c].render.submeshes.size()) << "chunk " << c;
    }
    // The root is the uncut box: one exterior submesh, no fracture faces.
    ASSERT_EQ(found->Submeshes(0).size(), 1u);
    EXPECT_FALSE(found->Submeshes(0)[0].interior);
    EXPECT_EQ(found->Submeshes(0)[0].mesh.indexCount, 36u);
}

TEST_F(ChunkGathererTest, IntactWall_DrawsRootMeshOnly)
{
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    Cache(wall);
    const DestructibleId id = scene.Spawn(wall);
    scene.Step(2);

    const ChunkGatherStats stats = gatherer.Gather(scene.world, scene.destruction, *cache, materials, objects, 1000);
    EXPECT_EQ(stats.pieces, 1u);
    EXPECT_EQ(stats.chunks, 1u);
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_TRUE(objects[0].occluder);
    EXPECT_EQ(objects[0].mesh.indexCount, 36u);
    EXPECT_EQ(scene.destruction.PieceChunks(id, 0).size(), cache->Find(wall.get())->renderSet.LeafCount(0));
}

TEST_F(ChunkGathererTest, AfterBreak_OnlyBrokenRegionSplit)
{
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    Cache(wall);
    const DestructibleId id = scene.Spawn(wall);
    scene.Step(2);
    // Break every bond of the highest leaf, so it falls away from its region.
    std::uint32_t top = 0;
    for (std::uint32_t c = 0; c < wall->chunks.size(); ++c)
    {
        if (wall->chunks[c].childCount == 0 && wall->chunks[c].centroid.y > wall->chunks[top].centroid.y)
        {
            top = c;
        }
    }
    for (const Bond& bond : wall->bonds)
    {
        if (bond.chunkA == top || bond.chunkB == top)
        {
            scene.destruction.QueueDamage(
                {.center = bond.centroid, .energy = 1.0e9f, .innerRadius = 0.01f, .radius = 0.02f}, DamageKind::Melee);
        }
    }
    scene.Step(2);

    std::uint32_t leaves = 0;
    std::uint32_t pieces = 0;
    for (std::uint32_t piece = 0; piece < scene.destruction.PieceCount(id); ++piece)
    {
        const auto chunks = scene.destruction.PieceChunks(id, piece);
        leaves += static_cast<std::uint32_t>(chunks.size());
        pieces += chunks.empty() ? 0u : 1u;
    }
    ASSERT_GT(pieces, 1u) << "the damage broke nothing off";

    const ChunkGatherStats stats = gatherer.Gather(scene.world, scene.destruction, *cache, materials, objects, 1000);
    EXPECT_EQ(stats.pieces, pieces);
    // The three untouched regions still draw as one mesh each; only the broken region falls back to its (at most
    // ten) leaves, and the fallen leaf is drawn on its own.
    EXPECT_LE(stats.chunks, 3u + 10u + 1u);
    EXPECT_LT(stats.chunks + 10, leaves);
    // Broken-off leaves show their fracture faces.
    EXPECT_TRUE(std::ranges::any_of(objects, [](const render::RenderObject& o) { return !o.occluder; }));
    const AssetMeshes& wallMeshes = *cache->Find(wall.get());
    std::size_t interior = 0;
    for (std::uint32_t c = 0; c < wall->chunks.size(); ++c)
    {
        for (const ChunkSubmesh& submesh : wallMeshes.Submeshes(c))
        {
            interior += submesh.interior ? 1 : 0;
        }
    }
    EXPECT_GT(interior, 0u);
}

TEST_F(ChunkGathererTest, UncachedAsset_Skipped)
{
    DestructionScene scene;
    (void)scene.Spawn(MakeFracturedWall(scene.jobs));
    scene.Step();
    const ChunkGatherStats stats = gatherer.Gather(scene.world, scene.destruction, *cache, materials, objects, 1000);
    EXPECT_TRUE(objects.empty());
    EXPECT_EQ(stats.uncached, 1u);
}

TEST_F(ChunkGathererTest, MaxObjects_DropsTheRest)
{
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    Cache(wall);
    (void)scene.Spawn(wall);
    (void)scene.Spawn(wall, {6.0f, 0.0f, 0.0f});
    scene.Step();
    objects.resize(4);
    const ChunkGatherStats stats = gatherer.Gather(scene.world, scene.destruction, *cache, materials, objects, 5);
    EXPECT_EQ(objects.size(), 5u);
    EXPECT_EQ(stats.dropped, 1u);
}

TEST_F(ChunkGathererTest, DebrisHandoff_DrawsDebrisWithItsChunks)
{
    DestructionSettings settings;
    settings.smallPieceVolume = 1.0f; // every wall leaf (about 0.15 m^3) counts as small
    DestructionScene scene(settings);
    scene.debris = std::make_unique<DebrisSystem>(scene.physics);
    scene.destruction.SetDebrisSystem(scene.debris.get());
    const auto wall = MakeFracturedWall(scene.jobs);
    Cache(wall);
    (void)scene.Spawn(wall);
    scene.Step(2);
    scene.destruction.QueueDamage(
        {.center = {0.0f, 1.5f, 0.0f}, .energy = 1.0e12f, .innerRadius = 5.0f, .radius = 5.0f}, DamageKind::Explosion);
    scene.Step(2);

    Query sources;
    sources.With<DebrisSourceComponent>();
    std::uint32_t sourced = 0;
    sources.ForEachChunk(scene.world,
                         [&](ChunkView& view)
                         {
                             for (const DebrisSourceComponent& source : view.Read<DebrisSourceComponent>())
                             {
                                 EXPECT_EQ(source.asset, wall);
                                 EXPECT_FALSE(source.chunks.empty());
                                 EXPECT_TRUE(std::ranges::is_sorted(source.chunks));
                                 ++sourced;
                             }
                         });
    ASSERT_GT(sourced, 0u) << "no piece was handed to the debris system";
    const ChunkGatherStats stats = gatherer.Gather(scene.world, scene.destruction, *cache, materials, objects, 100000);
    EXPECT_EQ(stats.debris, sourced);
}

TEST(BreakEffects, DestructionUpdate_SpawnsParticlesAtBrokenBonds)
{
    // The wall's material is concrete (interior look 0): blasting it spawns dust and chips near the blast.
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    (void)scene.Spawn(wall);
    scene.Step();
    BreakEffects effects;
    std::vector<render::Particle> particles;
    effects.Spawn(scene.destruction, particles);
    EXPECT_TRUE(particles.empty());

    scene.destruction.QueueDamage({.center = {0.0f, 1.5f, 0.0f}, .energy = 1.0e9f, .innerRadius = 0.6f, .radius = 0.9f},
                                  DamageKind::Explosion);
    scene.Step();
    ASSERT_FALSE(scene.destruction.BreakEvents().empty());
    effects.Spawn(scene.destruction, particles);
    ASSERT_GE(particles.size(), scene.destruction.BreakEvents().size() * 6u); // >= 4 dust + 2 chips per bond
    for (const render::Particle& p : particles)
    {
        EXPECT_LT(Length(p.position - Vec3(0.0f, 1.5f, 0.0f)), 2.5f);
        EXPECT_EQ(p.flags & render::kParticleAdditive, 0u);
    }
}

TEST(CrackDecals, DamagedBond_DecalOnTheBondInWorldSpace)
{
    // Weaken one bond to 30% health without breaking it: one crack decal sits on it; the wall is moved to x = 3.
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    const DestructibleId id = scene.Spawn(wall, {3.0f, 0.0f, 0.0f});
    scene.Step();
    std::vector<render::Decal> decals;
    GatherCrackDecals(scene.world, scene.destruction, decals);
    EXPECT_TRUE(decals.empty());

    const Bond& bond = wall->bonds[0];
    const float full = scene.destruction.BondFullHealth(id)[0];
    scene.destruction.QueueDamage({.center = bond.centroid + Vec3(3.0f, 0.0f, 0.0f),
                                   .energy = full * 0.7f,
                                   .innerRadius = 0.01f,
                                   .radius = 0.02f},
                                  DamageKind::Melee);
    scene.Step();
    ASSERT_FALSE(scene.destruction.IsBondBroken(id, 0));
    GatherCrackDecals(scene.world, scene.destruction, decals);
    ASSERT_EQ(decals.size(), 1u);
    EXPECT_NEAR(decals[0].intensity, CrackOverlayIntensity(full * 0.3f, full), 1e-5f);
    EXPECT_GT(decals[0].intensity, 0.5f);
    const Vec4 centre = decals[0].transform.columns[3];
    EXPECT_NEAR(centre.x, bond.centroid.x + 3.0f, 1e-4f);
    EXPECT_NEAR(centre.y, bond.centroid.y, 1e-4f);
    EXPECT_NEAR(centre.z, bond.centroid.z, 1e-4f);
    // Local z runs along the bond normal.
    const Vec3 z = NormalizeOrZero(
        Vec3(decals[0].transform.columns[2].x, decals[0].transform.columns[2].y, decals[0].transform.columns[2].z));
    EXPECT_NEAR(Dot(z, bond.normal), 1.0f, 1e-4f);
}

TEST(CrackDecals, SeparatedBond_NoDecal)
{
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    const DestructibleId id = scene.Spawn(wall);
    scene.Step();
    scene.destruction.QueueDamage(
        {.center = {0.6f, 1.8f, -0.4f}, .energy = 1.0e9f, .innerRadius = 0.9f, .radius = 1.4f}, DamageKind::Explosion);
    scene.Step(3);
    std::vector<render::Decal> decals;
    GatherCrackDecals(scene.world, scene.destruction, decals);
    // Every broken bond either separated pieces (no decal) or is inside one piece (full crack).
    std::uint32_t brokenTogether = 0;
    std::vector<std::uint32_t> pieceOf(wall->chunks.size(), ~0u);
    for (std::uint32_t piece = 0; piece < scene.destruction.PieceCount(id); ++piece)
    {
        for (const std::uint32_t chunk : scene.destruction.PieceChunks(id, piece))
        {
            pieceOf[chunk] = piece;
        }
    }
    for (std::uint32_t bond = 0; bond < wall->bonds.size(); ++bond)
    {
        const Bond& b = wall->bonds[bond];
        if (scene.destruction.IsBondBroken(id, bond) && pieceOf[b.chunkA] != ~0u &&
            pieceOf[b.chunkA] == pieceOf[b.chunkB])
        {
            ++brokenTogether;
        }
    }
    const auto full = static_cast<std::uint32_t>(
        std::ranges::count_if(decals, [](const render::Decal& d) { return d.intensity == 1.0f; }));
    EXPECT_GE(full, brokenTogether);
    EXPECT_LT(decals.size(), wall->bonds.size());
}

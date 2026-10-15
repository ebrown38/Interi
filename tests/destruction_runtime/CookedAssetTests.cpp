// SPDX-License-Identifier: MIT
// A Voronoi-fractured, multi-level asset (non-leaf chunks, multi-hull chunks, arbitrary bond normals) loaded through
// the file system and simulated: the runtime paths grid assets don't reach.
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/physics/Queries.h>
#include <interi/platform/FileSystem.h>

#include "ScenarioUtil.h"
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

/// An L-shaped concrete wall 0.4 m thick standing on y = 0: 4 m long, 1 m tall, with a 1 m wide post rising to 3 m
/// at x in [0, 1]. Fractured in two levels and cooked like fracture_cooker would.
std::shared_ptr<const DestructibleAsset> CookLWall(JobSystem& jobs)
{
    static constexpr std::array<std::array<float, 2>, 6> kOutline{{{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 3}, {0, 3}}};
    const FractureMesh source = MakePrismMesh(kOutline, 0.0f, 0.4f);
    HierarchySettings hierarchy;
    for (const std::uint32_t count : {6u, 4u})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = 7 + count;
        hierarchy.levels.push_back(level);
    }
    const ChunkHierarchy chunks = BuildHierarchy(source, hierarchy, jobs);
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    BondSettings bonds;
    bonds.groundHeight = 0.0f;
    BondGraph graph = BuildBondGraph(chunks.chunks, materials, bonds);
    Result<DestructibleAsset> asset = AssembleAsset(chunks.chunks, std::move(graph), materials, {}, jobs);
    EXPECT_TRUE(asset) << asset.error().message;

    MemoryFileSystem files;
    EXPECT_TRUE(files.WriteFile("walls/l.idest", WriteDestructibleAsset(*asset, 1)));
    Result<std::shared_ptr<const DestructibleAsset>> loaded = LoadDestructibleAsset(files, "walls/l.idest");
    EXPECT_TRUE(loaded);
    return *loaded;
}

} // namespace

TEST(CookedAsset, Spawn_StandsAndMapsHitsToLeaves)
{
    Scenario scenario(4);
    const std::shared_ptr<const DestructibleAsset> asset = CookLWall(scenario.jobs);
    ASSERT_GT(asset->chunks.size(), 7u);
    ASSERT_FALSE(asset->anchors.empty());
    std::size_t multiHull = 0;
    for (const AssetChunk& chunk : asset->chunks)
    {
        multiHull += chunk.childCount == 0 && chunk.hulls.size() > 1 ? 1 : 0;
    }
    const DestructibleId id = scenario.destruction.Spawn(scenario.world, scenario.jobs, asset, {});
    for (int frame = 0; frame < 60; ++frame)
    {
        scenario.Step();
        ASSERT_EQ(scenario.destruction.LastStats().bondsBrokenByStress, 0u) << "frame " << frame;
    }
    for (std::uint32_t piece = 0; piece < scenario.destruction.PieceCount(id); ++piece)
    {
        for (const std::uint32_t chunk : scenario.destruction.PieceChunks(id, piece))
        {
            EXPECT_EQ(asset->chunks[chunk].childCount, 0u) << "pieces hold leaves only";
        }
    }
    const std::optional<physics::QueryHit> hit =
        physics::Raycast(scenario.physicsWorld, Vec3(0.5f, 10.0f, 0.2f), Vec3(0.0f, -20.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    const std::optional<std::uint32_t> chunk = scenario.destruction.ChunkAt(scenario.world, hit->entity, hit->subShape);
    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(asset->chunks[*chunk].childCount, 0u);
    EXPECT_GT(multiHull, 0u) << "the L-shaped cells should need a split hull somewhere";
}

TEST(CookedAsset, Explosion_BreaksOffFinitePieces)
{
    Scenario scenario(4);
    const std::shared_ptr<const DestructibleAsset> asset = CookLWall(scenario.jobs);
    const DestructibleId id = scenario.destruction.Spawn(scenario.world, scenario.jobs, asset, {});
    scenario.Step();
    // A charge at the foot of the post.
    scenario.destruction.QueueDamage(
        {.center = {0.5f, 1.0f, 0.2f}, .energy = 5.0e4f, .innerRadius = 0.6f, .radius = 1.2f}, DamageKind::Explosion);
    std::uint32_t spawned = 0;
    for (int frame = 0; frame < 120; ++frame)
    {
        scenario.Step();
        spawned += scenario.destruction.LastStats().piecesSpawned;
        ASSERT_TRUE(std::isfinite(scenario.DynamicEnergy(id))) << "frame " << frame;
    }
    EXPECT_GT(spawned, 0u);
}

// SPDX-License-Identifier: MIT
#include <interi/destruction/BondGraph.h>
#include <interi/jobs/JobSystem.h>

#include "DestructionTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Vec3;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// Unfractured chunks (each its own root and leaf) from boxes.
std::vector<Chunk> BoxChunks(std::initializer_list<std::pair<Vec3, Vec3>> boxes, std::uint32_t material = 0)
{
    std::vector<Chunk> chunks;
    for (const auto& [min, max] : boxes)
    {
        Chunk chunk;
        chunk.mesh = MakeBoxMesh(min, max, material);
        chunk.material = material;
        chunk.volume = ComputeVolume(chunk.mesh);
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

std::vector<DestructionMaterial> Materials()
{
    return {DestructionMaterial{.name = "concrete"},
            DestructionMaterial{
                .name = "steel", .compressiveStrength = 2.5e8f, .tensileStrength = 2.5e8f, .shearStrength = 1.45e8f}};
}

/// Appends a fractured hierarchy's chunks to `all`, offsetting its indices.
void AppendHierarchy(std::vector<Chunk>& all, ChunkHierarchy hierarchy)
{
    const auto offset = static_cast<std::uint32_t>(all.size());
    for (Chunk& chunk : hierarchy.chunks)
    {
        if (chunk.parent != kNoChunk)
        {
            chunk.parent += offset;
        }
        chunk.firstChild += chunk.childCount > 0 ? offset : 0;
        all.push_back(std::move(chunk));
    }
}

} // namespace

TEST(BondGraph, TwoAdjacentCubes_OneBondWithFaceArea)
{
    const auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}, {{1, 0, 0}, {2, 1, 1}}});
    const auto materials = Materials();
    const BondGraph graph = BuildBondGraph(chunks, materials, {});
    ASSERT_EQ(graph.bonds.size(), 1u);
    const Bond& bond = graph.bonds[0];
    EXPECT_EQ(bond.chunkA, 0u);
    EXPECT_EQ(bond.chunkB, 1u);
    EXPECT_NEAR(bond.area, 1.0f, 1e-5f);
    EXPECT_NEAR(bond.centroid.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bond.centroid.y, 0.5f, 1e-5f);
    EXPECT_NEAR(bond.centroid.z, 0.5f, 1e-5f);
    EXPECT_NEAR(bond.normal.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bond.tensileLimit, materials[0].tensileStrength, 1.0f);
    EXPECT_TRUE(graph.anchors.empty());
}

TEST(BondGraph, PartialOverlap_AreaOfOverlapOnly)
{
    // A 1 x 1 face against a 2 x 2 face, offset so a quarter of the small face hangs off.
    const auto chunks = BoxChunks({{{0, 0, 0}, {2, 2, 2}}, {{2, 1.5f, 1.5f}, {3, 2.5f, 2.5f}}});
    const BondGraph graph = BuildBondGraph(chunks, Materials(), {});
    ASSERT_EQ(graph.bonds.size(), 1u);
    EXPECT_NEAR(graph.bonds[0].area, 0.25f, 1e-5f);
}

TEST(BondGraph, SeparatedCubes_NoBond)
{
    const auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}, {{1.01f, 0, 0}, {2, 1, 1}}});
    EXPECT_TRUE(BuildBondGraph(chunks, Materials(), {}).bonds.empty());
}

TEST(BondGraph, EdgeContactOnly_NoBond)
{
    const auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}, {{1, 1, 0}, {2, 2, 1}}});
    EXPECT_TRUE(BuildBondGraph(chunks, Materials(), {}).bonds.empty());
}

TEST(BondGraph, MixedMaterials_WeakerLimitWins)
{
    auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}});
    auto steel = BoxChunks({{{0, 1, 0}, {1, 2, 1}}}, 1);
    chunks.push_back(std::move(steel[0]));
    const auto materials = Materials();
    const BondGraph graph = BuildBondGraph(chunks, materials, {});
    ASSERT_EQ(graph.bonds.size(), 1u);
    EXPECT_NEAR(graph.bonds[0].shearLimit, materials[0].shearStrength, 1.0f);
}

TEST(BondGraph, AnchorDetection_GroundContactMarked)
{
    // Two stacked cubes on the ground at y = 0 and one floating.
    const auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}, {{0, 1, 0}, {1, 2, 1}}, {{3, 0.5f, 0}, {4, 1.5f, 1}}});
    BondSettings settings;
    settings.groundHeight = 0.0f;
    const BondGraph graph = BuildBondGraph(chunks, Materials(), settings);
    EXPECT_EQ(graph.anchors, std::vector<std::uint32_t>{0});
    EXPECT_EQ(graph.bonds.size(), 1u);
}

TEST(BondGraph, AnchorMesh_TouchingChunkMarked)
{
    const auto chunks = BoxChunks({{{0, 0, 0}, {1, 1, 1}}, {{5, 0, 0}, {6, 1, 1}}});
    const std::vector<FractureMesh> terrain{MakeBoxMesh({4, -1, -1}, {7, 0, 2})};
    const BondGraph graph = BuildBondGraph(chunks, Materials(), {}, terrain);
    EXPECT_EQ(graph.anchors, std::vector<std::uint32_t>{1});
}

TEST(BondGraph, FracturedCube_BondsBetweenTouchingLeaves)
{
    JobSystem jobs(Workers(2));
    HierarchySettings settings;
    SeedSettings level;
    level.count = 12;
    settings.levels.push_back(level);
    const ChunkHierarchy hierarchy = BuildHierarchy(MakeBoxMesh({0, 0, 0}, {1, 1, 1}), settings, jobs);
    const BondGraph graph = BuildBondGraph(hierarchy.chunks, Materials(), {});
    EXPECT_EQ(CountLeafComponents(hierarchy.chunks, graph.bonds), 1u);
    for (const Bond& bond : graph.bonds)
    {
        EXPECT_TRUE(hierarchy.chunks[bond.chunkA].IsLeaf());
        EXPECT_LT(bond.chunkA, bond.chunkB);
        EXPECT_GT(bond.area, 0.0f);
    }
    // Total bond area is half the total interior (cut) area: every cut face is shared by two leaves.
    double interior = 0.0;
    for (const Chunk& chunk : hierarchy.chunks)
    {
        interior += chunk.IsLeaf() ? ComputeInteriorArea(chunk.mesh) : 0.0;
    }
    double bonded = 0.0;
    for (const Bond& bond : graph.bonds)
    {
        bonded += static_cast<double>(bond.area);
    }
    EXPECT_NEAR(bonded, interior * 0.5, interior * 1e-3);
}

TEST(BondGraph, Building_SingleConnectedComponent)
{
    // Floor slab, four walls standing on it and a roof slab, each fractured separately.
    JobSystem jobs(Workers(4));
    const std::vector<std::pair<Vec3, Vec3>> parts{
        {{0, 0, 0}, {6, 0.3f, 6}},             // floor
        {{0, 0.3f, 0}, {6, 3.3f, 0.3f}},       // south wall
        {{0, 0.3f, 5.7f}, {6, 3.3f, 6}},       // north wall
        {{0, 0.3f, 0.3f}, {0.3f, 3.3f, 5.7f}}, // west wall
        {{5.7f, 0.3f, 0.3f}, {6, 3.3f, 5.7f}}, // east wall
        {{0, 3.3f, 0}, {6, 3.6f, 6}},          // roof
    };
    std::vector<Chunk> chunks;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        HierarchySettings settings;
        SeedSettings level;
        level.count = 8;
        level.randomSeed = 40 + i;
        settings.levels.push_back(level);
        AppendHierarchy(chunks, BuildHierarchy(MakeBoxMesh(parts[i].first, parts[i].second), settings, jobs));
    }
    BondSettings settings;
    settings.groundHeight = 0.0f;
    const BondGraph graph = BuildBondGraph(chunks, Materials(), settings);
    EXPECT_EQ(CountLeafComponents(chunks, graph.bonds), 1u);
    ASSERT_FALSE(graph.anchors.empty());
    for (const std::uint32_t anchor : graph.anchors)
    {
        EXPECT_NEAR(ComputeBounds(chunks[anchor].mesh).min.y, 0.0f, 1e-5f) << "only floor chunks touch the ground";
    }
}

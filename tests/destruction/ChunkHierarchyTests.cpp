// SPDX-License-Identifier: MIT
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/jobs/JobSystem.h>

#include "DestructionTestUtil.h"
#include <gtest/gtest.h>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::JobSystem;
using interi::JobSystemDesc;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

HierarchySettings ThreeLevels(std::uint32_t maxDepth)
{
    HierarchySettings settings;
    for (const std::uint32_t count : {3u, 4u, 3u})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = 100 + count;
        settings.levels.push_back(level);
    }
    settings.maxDepth = maxDepth;
    return settings;
}

} // namespace

TEST(Hierarchy, ChildrenVolumes_SumToParent)
{
    JobSystem jobs(Workers(2));
    const ChunkHierarchy hierarchy = BuildHierarchy(MakeLShape(), ThreeLevels(2), jobs);
    ASSERT_GT(hierarchy.chunks.size(), 1u);
    EXPECT_NEAR(hierarchy.chunks[0].volume, 3.0, 1e-5);
    for (const Chunk& chunk : hierarchy.chunks)
    {
        if (chunk.IsLeaf())
        {
            continue;
        }
        double sum = 0.0;
        for (std::uint32_t c = chunk.firstChild; c < chunk.firstChild + chunk.childCount; ++c)
        {
            EXPECT_EQ(hierarchy.chunks[c].level, chunk.level + 1);
            sum += hierarchy.chunks[c].volume;
        }
        EXPECT_NEAR(sum, chunk.volume, chunk.volume * 1e-3);
    }
}

TEST(Hierarchy, DepthLimit_Respected)
{
    JobSystem jobs(Workers(2));
    const FractureMesh cube = MakeBoxMesh({0, 0, 0}, {1, 1, 1});
    EXPECT_EQ(BuildHierarchy(cube, ThreeLevels(0), jobs).chunks.size(), 1u);
    EXPECT_EQ(BuildHierarchy(cube, ThreeLevels(1), jobs).Depth(), 1u);
    EXPECT_EQ(BuildHierarchy(cube, ThreeLevels(2), jobs).Depth(), 2u);
    EXPECT_EQ(BuildHierarchy(cube, ThreeLevels(9), jobs).Depth(), 3u); // only three levels configured
}

TEST(Hierarchy, Layout_BreadthFirstWithContiguousChildren)
{
    JobSystem jobs(Workers(2));
    const ChunkHierarchy hierarchy = BuildHierarchy(MakeUShape(), ThreeLevels(3), jobs);
    for (std::uint32_t i = 1; i < hierarchy.chunks.size(); ++i)
    {
        const Chunk& chunk = hierarchy.chunks[i];
        ASSERT_NE(chunk.parent, kNoChunk);
        EXPECT_LT(chunk.parent, i);
        const Chunk& parent = hierarchy.chunks[chunk.parent];
        EXPECT_GE(i, parent.firstChild);
        EXPECT_LT(i, parent.firstChild + parent.childCount);
        EXPECT_GE(chunk.level, hierarchy.chunks[i - 1].level);
        EXPECT_TRUE(IsWatertight(chunk.mesh));
    }
}

TEST(Hierarchy, ThreadCount_IdenticalHierarchy)
{
    JobSystem serial(Workers(0));
    JobSystem parallel(Workers(6));
    const ChunkHierarchy a = BuildHierarchy(MakeLShape(), ThreeLevels(3), serial);
    const ChunkHierarchy b = BuildHierarchy(MakeLShape(), ThreeLevels(3), parallel);
    ASSERT_EQ(a.chunks.size(), b.chunks.size());
    for (std::size_t i = 0; i < a.chunks.size(); ++i)
    {
        EXPECT_EQ(a.chunks[i].mesh, b.chunks[i].mesh);
        EXPECT_EQ(a.chunks[i].parent, b.chunks[i].parent);
    }
}

TEST(Hierarchy, DominantMaterial_LargestArea)
{
    FractureMesh mesh = MakeBoxMesh({0, 0, 0}, {1, 1, 1}, 2);
    AppendMesh(mesh, MakeBoxMesh({2, 0, 0}, {2.5f, 0.5f, 0.5f}, 5));
    EXPECT_EQ(DominantMaterial(mesh), 2u);
}

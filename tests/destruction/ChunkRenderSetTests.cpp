// SPDX-License-Identifier: MIT
#include <interi/destruction/ChunkRenderSet.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace interi::destruction;

namespace
{

/// One object, breadth-first: root 0, regions 1..4, and four leaves per region (region r owns 1 + 4r .. 4 + 4r).
/// Then a second object made of a single leaf chunk (21).
AssetChunk Node(std::uint32_t parent, std::uint32_t firstChild, std::uint32_t childCount)
{
    AssetChunk chunk;
    chunk.parent = parent;
    chunk.firstChild = firstChild;
    chunk.childCount = childCount;
    return chunk;
}

std::vector<AssetChunk> TwoLevelHierarchy()
{
    std::vector<AssetChunk> chunks(22);
    chunks[0] = Node(kNoChunk, 1, 4);
    for (std::uint32_t region = 1; region <= 4; ++region)
    {
        chunks[region] = Node(0, 1 + 4 * region, 4);
        for (std::uint32_t leaf = 1 + 4 * region; leaf <= 4 + 4 * region; ++leaf)
        {
            chunks[leaf] = Node(region, 0, 0);
        }
    }
    chunks[21] = Node(kNoChunk, 0, 0);
    return chunks;
}

std::vector<std::uint32_t> Range(std::uint32_t first, std::uint32_t last)
{
    std::vector<std::uint32_t> values;
    for (std::uint32_t v = first; v <= last; ++v)
    {
        values.push_back(v);
    }
    return values;
}

std::vector<std::uint32_t> Cover(ChunkRenderSet& set, const std::vector<std::uint32_t>& leaves)
{
    std::vector<std::uint32_t> out;
    set.Cover(leaves, out);
    return out;
}

} // namespace

TEST(ChunkRenderSet, LeafCounts_CountLeavesUnderEachChunk)
{
    const auto chunks = TwoLevelHierarchy();
    const ChunkRenderSet set(chunks);
    EXPECT_EQ(set.LeafCount(0), 16u);
    EXPECT_EQ(set.LeafCount(3), 4u);
    EXPECT_EQ(set.LeafCount(7), 1u);
    EXPECT_EQ(set.LeafCount(21), 1u);
}

TEST(ChunkRenderSet, IntactStructure_SingleDrawBatch)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    EXPECT_EQ(Cover(set, Range(5, 20)), std::vector<std::uint32_t>{0});
}

TEST(ChunkRenderSet, AfterBreak_OnlyBrokenRegionSplit)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    // Region 2 (leaves 9..12) cracked: leaves 11 and 12 fell away, the rest still stands.
    std::vector<std::uint32_t> standing = Range(5, 10);
    for (const std::uint32_t leaf : Range(13, 20))
    {
        standing.push_back(leaf);
    }
    EXPECT_EQ(Cover(set, standing), (std::vector<std::uint32_t>{1, 3, 4, 9, 10}));
    EXPECT_EQ(Cover(set, {12, 11}), (std::vector<std::uint32_t>{11, 12}));
}

TEST(ChunkRenderSet, WholeRegion_DrawsRegionChunk)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    EXPECT_EQ(Cover(set, {16, 13, 15, 14}), std::vector<std::uint32_t>{3});
}

TEST(ChunkRenderSet, SeveralObjects_EachRootDrawn)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    std::vector<std::uint32_t> all = Range(5, 21);
    EXPECT_EQ(Cover(set, all), (std::vector<std::uint32_t>{0, 21}));
}

TEST(ChunkRenderSet, RepeatedCalls_ScratchReset)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    EXPECT_EQ(Cover(set, Range(5, 8)), std::vector<std::uint32_t>{1});
    EXPECT_EQ(Cover(set, Range(5, 8)), std::vector<std::uint32_t>{1});
    EXPECT_EQ(Cover(set, {5}), std::vector<std::uint32_t>{5});
}

TEST(ChunkRenderSet, AppendsAfterExistingOutput)
{
    const auto chunks = TwoLevelHierarchy();
    ChunkRenderSet set(chunks);
    std::vector<std::uint32_t> out{99};
    set.Cover(std::vector<std::uint32_t>{6, 5}, out);
    EXPECT_EQ(out, (std::vector<std::uint32_t>{99, 5, 6}));
}

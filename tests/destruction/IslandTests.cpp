// SPDX-License-Identifier: MIT
#include <interi/destruction/Islands.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/DeterministicRng.h>

#include "StressTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

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

std::vector<GraphEdge> Edges(const StressGraph& graph)
{
    std::vector<GraphEdge> edges;
    for (const StressBond& bond : graph.bonds)
    {
        edges.emplace_back(bond.nodeA, bond.nodeB);
    }
    return edges;
}

std::vector<std::uint8_t> Anchored(const StressGraph& graph)
{
    std::vector<std::uint8_t> anchored;
    for (const StressNode& node : graph.nodes)
    {
        anchored.push_back(node.anchored ? 1 : 0);
    }
    return anchored;
}

/// Reference labels: repeated relaxation to the minimum over edges (slow but obviously right).
std::vector<std::uint32_t> ReferenceLabels(std::uint32_t nodeCount, const std::vector<GraphEdge>& edges,
                                           const std::vector<std::uint8_t>& broken)
{
    std::vector<std::uint32_t> labels(nodeCount);
    std::iota(labels.begin(), labels.end(), 0u);
    for (bool changed = true; changed;)
    {
        changed = false;
        for (std::size_t e = 0; e < edges.size(); ++e)
        {
            if (broken[e] != 0)
            {
                continue;
            }
            std::uint32_t& a = labels[edges[e].first];
            std::uint32_t& b = labels[edges[e].second];
            if (a != b)
            {
                a = b = std::min(a, b);
                changed = true;
            }
        }
    }
    return labels;
}

class UnionFind : public interi::testing::SeededTest
{
};

} // namespace

TEST_F(UnionFind, Parallel_MatchesSerial)
{
    constexpr std::uint32_t kNodes = 50000;
    std::vector<GraphEdge> edges;
    std::vector<std::uint8_t> broken;
    for (int i = 0; i < 60000; ++i)
    {
        const auto a = static_cast<std::uint32_t>(Rng().NextInt(0, kNodes - 1));
        // Mostly local edges, so components are many and of mixed size.
        const auto b = static_cast<std::uint32_t>(std::min<std::int64_t>(kNodes - 1, a + Rng().NextInt(1, 40)));
        edges.emplace_back(a, b);
        broken.push_back(Rng().NextInt(0, 3) == 0 ? 1 : 0);
    }
    std::vector<std::uint32_t> serial(kNodes);
    std::vector<std::uint32_t> parallel(kNodes);
    JobSystem one(Workers(0));
    JobSystem many(Workers(8));
    LabelComponents(one, kNodes, edges, broken, serial);
    LabelComponents(many, kNodes, edges, broken, parallel);
    EXPECT_EQ(serial, parallel) << "seed " << Seed();
    EXPECT_EQ(serial, ReferenceLabels(kNodes, edges, broken)) << "seed " << Seed();
}

TEST(Islands, CutBridgeMiddle_TwoAnchoredHalves)
{
    JobSystem jobs(Workers(4));
    GridOptions options;
    options.anchored = [](const Cell& cell) { return cell[0] == 0 || cell[0] == 8; };
    const GridStructure bridge = MakeGrid(BoxCells(9, 2, 1), options);
    const std::vector<GraphEdge> edges = Edges(bridge.graph);
    std::vector<std::uint8_t> broken(edges.size(), 0);
    for (const int y : {0, 1})
    {
        broken[*bridge.Bond({4, y, 0}, {5, y, 0})] = 1;
    }
    std::vector<std::uint32_t> labels(bridge.graph.nodes.size());
    LabelComponents(jobs, static_cast<std::uint32_t>(labels.size()), edges, broken, labels);
    const std::vector<Island> islands = GroupIslands(labels, Anchored(bridge.graph));
    ASSERT_EQ(islands.size(), 2u);
    EXPECT_TRUE(islands[0].anchored);
    EXPECT_TRUE(islands[1].anchored);
    EXPECT_EQ(islands[0].nodes.size(), 10u);
    EXPECT_EQ(islands[1].nodes.size(), 8u);
    EXPECT_EQ(islands[0].nodes.front(), 0u);
}

TEST(Islands, CutAllSupports_WholeStructureFalls)
{
    JobSystem jobs(Workers(4));
    // A 3x4x1 block standing on the ground row (anchored).
    const GridStructure block = MakeGrid(BoxCells(3, 4, 1));
    const std::vector<GraphEdge> edges = Edges(block.graph);
    std::vector<std::uint8_t> broken(edges.size(), 0);
    for (int x = 0; x < 3; ++x)
    {
        broken[*block.Bond({x, 0, 0}, {x, 1, 0})] = 1;
    }
    std::vector<std::uint32_t> labels(block.graph.nodes.size());
    LabelComponents(jobs, static_cast<std::uint32_t>(labels.size()), edges, broken, labels);
    const std::vector<Island> islands = GroupIslands(labels, Anchored(block.graph));
    ASSERT_EQ(islands.size(), 2u);
    EXPECT_TRUE(islands[0].anchored);
    EXPECT_EQ(islands[0].nodes, (std::vector<std::uint32_t>{0, 1, 2}));
    EXPECT_FALSE(islands[1].anchored);
    EXPECT_EQ(islands[1].nodes.size(), 9u);
}

TEST(Islands, Include_SkipsExcludedNodes)
{
    const std::vector<std::uint32_t> labels{0, 0, 2, 2};
    const std::vector<std::uint8_t> anchored{0, 0, 1, 0};
    const std::vector<std::uint8_t> include{0, 1, 0, 1};
    const std::vector<Island> islands = GroupIslands(labels, anchored, include);
    ASSERT_EQ(islands.size(), 2u);
    EXPECT_EQ(islands[0].nodes, std::vector<std::uint32_t>{1});
    EXPECT_EQ(islands[1].nodes, std::vector<std::uint32_t>{3});
    EXPECT_FALSE(islands[1].anchored); // the anchor (node 2) is excluded
}

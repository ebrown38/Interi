// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/TaskGraph.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

using interi::JobSystem;
using interi::JobSystemDesc;
using interi::TaskGraph;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

std::atomic<int> g_assertCount{0};

interi::AssertAction CountAssert(const interi::AssertInfo& /*info*/)
{
    g_assertCount.fetch_add(1);
    return interi::AssertAction::Continue;
}

} // namespace

TEST(TaskGraph, Empty_RunReturns)
{
    JobSystem jobs(Workers(2));
    TaskGraph graph;
    EXPECT_EQ(graph.NodeCount(), 0u);
    graph.Run(jobs);
}

TEST(TaskGraph, DiamondDependency_OrderRespected)
{
    JobSystem jobs(Workers(4));
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        std::atomic<int> clock{0};
        std::array<int, 4> stamp{};
        TaskGraph graph;
        const auto node = [&](std::size_t index)
        { return [&clock, &stamp, index] { stamp[index] = clock.fetch_add(1); }; };
        const TaskGraph::NodeId a = graph.AddNode(node(0));
        const TaskGraph::NodeId b = graph.AddNode(node(1));
        const TaskGraph::NodeId c = graph.AddNode(node(2));
        const TaskGraph::NodeId d = graph.AddNode(node(3));
        graph.AddDependency(a, b);
        graph.AddDependency(a, c);
        graph.AddDependency(b, d);
        graph.AddDependency(c, d);
        graph.Run(jobs);

        ASSERT_EQ(clock.load(), 4);
        EXPECT_LT(stamp[0], stamp[1]);
        EXPECT_LT(stamp[0], stamp[2]);
        EXPECT_LT(stamp[1], stamp[3]);
        EXPECT_LT(stamp[2], stamp[3]);
    }
}

TEST(TaskGraph, RunTwice_Reusable)
{
    JobSystem jobs(Workers(3));
    std::vector<std::atomic<int>> runs(50);
    TaskGraph graph;
    TaskGraph::NodeId previous = graph.AddNode([&runs] { runs[0].fetch_add(1); });
    for (std::size_t i = 1; i < runs.size(); ++i)
    {
        const TaskGraph::NodeId node = graph.AddNode([&runs, i] { runs[i].fetch_add(1); });
        if (i % 2 == 0)
        {
            graph.AddDependency(previous, node); // half the nodes form a chain, the rest are independent roots
            previous = node;
        }
    }
    graph.Run(jobs);
    graph.Run(jobs);
    for (const std::atomic<int>& count : runs)
    {
        EXPECT_EQ(count.load(), 2);
    }
}

TEST(TaskGraph, Chain_RunsInOrder)
{
    JobSystem jobs(Workers(4));
    std::vector<int> order;
    TaskGraph graph;
    TaskGraph::NodeId previous = 0;
    for (int i = 0; i < 100; ++i)
    {
        const TaskGraph::NodeId node = graph.AddNode([&order, i] { order.push_back(i); }); // chain: no data race
        if (i > 0)
        {
            graph.AddDependency(previous, node);
        }
        previous = node;
    }
    graph.Run(jobs);
    ASSERT_EQ(order.size(), 100u);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
    }
}

TEST(TaskGraph, ZeroWorkers_RunsOnCaller)
{
    JobSystem jobs(Workers(0));
    int sum = 0;
    TaskGraph graph;
    const TaskGraph::NodeId a = graph.AddNode([&sum] { sum += 1; });
    const TaskGraph::NodeId b = graph.AddNode([&sum] { sum *= 10; });
    graph.AddDependency(a, b);
    graph.Run(jobs);
    EXPECT_EQ(sum, 10);
}

TEST(TaskGraph, Cycle_AssertsInDebug)
{
    if (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "cycle detection is compiled out with asserts";
    }
    const interi::ScopedAssertHandler handler(&CountAssert);
    g_assertCount = 0;

    JobSystem jobs(Workers(2));
    std::atomic<int> ran{0};
    TaskGraph graph;
    const TaskGraph::NodeId a = graph.AddNode([&ran] { ran.fetch_add(1); });
    const TaskGraph::NodeId b = graph.AddNode([&ran] { ran.fetch_add(1); });
    const TaskGraph::NodeId c = graph.AddNode([&ran] { ran.fetch_add(1); });
    graph.AddDependency(a, b);
    graph.AddDependency(b, c);
    graph.AddDependency(c, a);
    graph.Run(jobs); // must return rather than hang

    EXPECT_EQ(g_assertCount.load(), 1);
    EXPECT_EQ(ran.load(), 0);
}

TEST(TaskGraph, SelfDependency_AssertsInDebug)
{
    if (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "cycle detection is compiled out with asserts";
    }
    const interi::ScopedAssertHandler handler(&CountAssert);
    g_assertCount = 0;

    TaskGraph graph;
    const TaskGraph::NodeId a = graph.AddNode([] {});
    graph.AddDependency(a, a);
    JobSystem jobs(Workers(1));
    graph.Run(jobs);
    EXPECT_GE(g_assertCount.load(), 1);
}

TEST(TaskGraph, Clear_RemovesNodes)
{
    JobSystem jobs(Workers(1));
    int ran = 0;
    TaskGraph graph;
    graph.AddNode([&ran] { ++ran; });
    graph.Clear();
    EXPECT_EQ(graph.NodeCount(), 0u);
    graph.AddNode([&ran] { ran += 10; });
    graph.Run(jobs);
    EXPECT_EQ(ran, 10);
}

// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/ecs/Scheduler.h>

#include "EcsTestUtil.h"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using interi::ChunkView;
using interi::CommandBuffers;
using interi::ErrorCode;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Query;
using interi::Scheduler;
using interi::SystemAccess;
using interi::SystemContext;
using interi::World;
using interi::testing::CbHealth;
using interi::testing::CbPosition;
using interi::testing::CbVelocity;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// Records which systems ran in which order.
class Trace
{
public:
    void Add(int id)
    {
        const std::scoped_lock lock(m_mutex);
        m_order.push_back(id);
    }
    [[nodiscard]] std::size_t IndexOf(int id) const
    {
        return static_cast<std::size_t>(std::ranges::find(m_order, id) - m_order.begin());
    }

private:
    std::mutex m_mutex;
    std::vector<int> m_order;
};

std::atomic<int> g_asserts{0};

interi::AssertAction CountAssert(const interi::AssertInfo& /*info*/)
{
    g_asserts.fetch_add(1);
    return interi::AssertAction::Continue;
}

} // namespace

TEST(Scheduler, ReadOnlySystems_RunConcurrently)
{
    World world;
    JobSystem jobs(Workers(2));
    CommandBuffers commands(jobs);
    std::atomic<int> arrived{0};
    std::atomic<int> overlapped{0};
    const auto rendezvous = [&](SystemContext&)
    {
        // Wait (bounded) until the other reader is running at the same time; fail rather than hang.
        arrived.fetch_add(1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (arrived.load() < 2 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (arrived.load() >= 2)
        {
            overlapped.fetch_add(1);
        }
    };
    Scheduler scheduler;
    const auto a = scheduler.AddSystem("ReadA", SystemAccess{}.Read<CbPosition>(), rendezvous);
    const auto b = scheduler.AddSystem("ReadB", SystemAccess{}.Read<CbPosition>(), rendezvous);
    ASSERT_TRUE(scheduler.Build().has_value());
    EXPECT_FALSE(scheduler.IsOrdered(a, b));
    EXPECT_FALSE(scheduler.IsOrdered(b, a));
    scheduler.Run(world, jobs, commands);
    EXPECT_EQ(overlapped.load(), 2);
}

TEST(Scheduler, WriteConflict_Serialized)
{
    World world;
    JobSystem jobs(Workers(4));
    CommandBuffers commands(jobs);
    std::atomic<int> running{0};
    std::atomic<int> overlaps{0};
    std::atomic<int> ranFirst{-1};
    const auto make = [&](int id)
    {
        return [&, id](SystemContext&)
        {
            int expected = -1;
            ranFirst.compare_exchange_strong(expected, id);
            if (running.fetch_add(1) != 0)
            {
                overlaps.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            running.fetch_sub(1);
        };
    };
    Scheduler scheduler;
    const auto writer = scheduler.AddSystem("Writer", SystemAccess{}.Write<CbPosition>(), make(0));
    const auto reader = scheduler.AddSystem("Reader", SystemAccess{}.Read<CbPosition>(), make(1));
    ASSERT_TRUE(scheduler.Build().has_value());
    EXPECT_TRUE(scheduler.IsOrdered(writer, reader));
    for (int i = 0; i < 50; ++i)
    {
        ranFirst = -1;
        scheduler.Run(world, jobs, commands);
        EXPECT_EQ(ranFirst.load(), 0);
    }
    EXPECT_EQ(overlaps.load(), 0);
}

TEST(Scheduler, DisjointWrites_NotOrdered)
{
    Scheduler scheduler;
    const auto a = scheduler.AddSystem("A", SystemAccess{}.Write<CbPosition>(), [](SystemContext&) {});
    const auto b = scheduler.AddSystem("B", SystemAccess{}.Write<CbVelocity>().Read<CbHealth>(), [](SystemContext&) {});
    ASSERT_TRUE(scheduler.Build().has_value());
    EXPECT_FALSE(scheduler.IsOrdered(a, b));
    EXPECT_FALSE(scheduler.IsOrdered(b, a));
}

TEST(Scheduler, CycleInDeclaredOrder_Error)
{
    Scheduler scheduler;
    const auto a = scheduler.AddSystem("Alpha", SystemAccess{}, [](SystemContext&) {});
    const auto b = scheduler.AddSystem("Beta", SystemAccess{}, [](SystemContext&) {});
    const auto c = scheduler.AddSystem("Gamma", SystemAccess{}, [](SystemContext&) {});
    scheduler.RunAfter(b, a);
    scheduler.RunAfter(c, b);
    scheduler.RunAfter(a, c);
    const auto result = scheduler.Build();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(result.error().message.find("Alpha"), std::string::npos);
    EXPECT_NE(result.error().message.find("Gamma"), std::string::npos);
}

TEST(Scheduler, ExplicitOrderAgainstRegistration_NoFalseCycle)
{
    // Registered A, B, C; explicit "A after C"; conflicts A-B and B-C. Valid order: C, A, B.
    World world;
    JobSystem jobs(Workers(3));
    CommandBuffers commands(jobs);
    Trace trace;
    Scheduler scheduler;
    const auto a = scheduler.AddSystem("A", SystemAccess{}.Write<CbPosition>(), [&](SystemContext&) { trace.Add(0); });
    const auto b = scheduler.AddSystem("B", SystemAccess{}.Read<CbPosition>().Write<CbHealth>(),
                                       [&](SystemContext&) { trace.Add(1); });
    const auto c = scheduler.AddSystem("C", SystemAccess{}.Read<CbHealth>(), [&](SystemContext&) { trace.Add(2); });
    scheduler.RunAfter(a, c);
    ASSERT_TRUE(scheduler.Build().has_value());
    EXPECT_TRUE(scheduler.IsOrdered(c, a));
    EXPECT_TRUE(scheduler.IsOrdered(a, b));
    EXPECT_TRUE(scheduler.IsOrdered(c, b));
    scheduler.Run(world, jobs, commands);
    EXPECT_LT(trace.IndexOf(2), trace.IndexOf(0));
    EXPECT_LT(trace.IndexOf(0), trace.IndexOf(1));
}

TEST(Scheduler, ChangedFilter_SeesWritesFromEarlierSystemOnce)
{
    World world;
    for (int i = 0; i < 2000; ++i)
    {
        (void)world.CreateEntity(CbPosition{}, CbHealth{i});
    }
    JobSystem jobs(Workers(2));
    CommandBuffers commands(jobs);
    std::atomic<bool> write{true};
    std::uint32_t changedRows = 0;
    Scheduler scheduler;
    Query all;
    all.With<CbPosition>();
    Query changed;
    changed.Changed<CbPosition>();
    (void)scheduler.AddSystem("Move", SystemAccess{}.Write<CbPosition>(),
                              [&](SystemContext& context)
                              {
                                  if (write)
                                  {
                                      context.ForEachChunk(all,
                                                           [](ChunkView& view) { (void)view.Write<CbPosition>(); });
                                  }
                              });
    (void)scheduler.AddSystem("Observe", SystemAccess{}.Read<CbPosition>(),
                              [&](SystemContext& context)
                              {
                                  changedRows = 0;
                                  context.ForEachChunk(changed, [&](ChunkView& view) { changedRows += view.Count(); });
                              });
    ASSERT_TRUE(scheduler.Build().has_value());
    scheduler.Run(world, jobs, commands);
    EXPECT_EQ(changedRows, 2000u);
    write = false;
    scheduler.Run(world, jobs, commands);
    EXPECT_EQ(changedRows, 0u);
    write = true;
    scheduler.Run(world, jobs, commands);
    EXPECT_EQ(changedRows, 2000u);
}

TEST(Scheduler, ParallelSystemCommands_DeterministicAcrossThreadCounts)
{
    const auto runWorld = [](std::uint32_t workers)
    {
        World world;
        for (int i = 0; i < 10000; ++i)
        {
            (void)world.CreateEntity(CbPosition{static_cast<float>(i)}, CbHealth{i});
        }
        JobSystem jobs(Workers(workers));
        CommandBuffers commands(jobs);
        Query query;
        query.With<CbHealth>();
        Scheduler scheduler;
        for (int system = 0; system < 2; ++system)
        {
            (void)scheduler.AddSystem(
                "Spawn" + std::to_string(system), SystemAccess{}.Read<CbHealth>(),
                [&query, system](SystemContext& context)
                {
                    context.ParallelForEachChunk(
                        query,
                        [&](ChunkView& view, std::size_t ordinal)
                        {
                            const auto health = view.Read<CbHealth>();
                            const auto entities = view.Entities();
                            for (std::uint32_t row = 0; row < view.Count(); ++row)
                            {
                                if ((health[row].value + system) % 7 == 0)
                                {
                                    context.Commands().DestroyEntity(context.CommandKey(ordinal), entities[row]);
                                }
                                else if (health[row].value % 5 == system)
                                {
                                    context.Commands().CreateEntity(context.CommandKey(ordinal),
                                                                    CbHealth{health[row].value}, CbVelocity{1});
                                }
                            }
                        });
                });
        }
        EXPECT_TRUE(scheduler.Build().has_value());
        for (int frame = 0; frame < 3; ++frame)
        {
            scheduler.Run(world, jobs, commands);
        }
        return interi::testing::HashWorld(world);
    };
    const std::uint64_t serial = runWorld(0);
    EXPECT_EQ(runWorld(4), serial);
    EXPECT_EQ(runWorld(31), serial);
}

TEST(Scheduler, UndeclaredWrite_Asserts)
{
    if (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts disabled";
    }
    World world;
    (void)world.CreateEntity(CbPosition{});
    JobSystem jobs(Workers(0));
    CommandBuffers commands(jobs);
    Query query;
    query.With<CbPosition>();
    Scheduler scheduler;
    (void)scheduler.AddSystem(
        "Sneaky", SystemAccess{}.Read<CbPosition>(), [&](SystemContext& context)
        { context.ForEachChunk(query, [](ChunkView& view) { (void)view.Write<CbPosition>(); }); });
    ASSERT_TRUE(scheduler.Build().has_value());
    g_asserts = 0;
    const interi::ScopedAssertHandler handler(&CountAssert);
    scheduler.Run(world, jobs, commands);
    EXPECT_GE(g_asserts.load(), 1);
}

TEST(Scheduler, UndeclaredFilter_Asserts)
{
    if (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts disabled";
    }
    World world;
    JobSystem jobs(Workers(0));
    CommandBuffers commands(jobs);
    Query query;
    query.Changed<CbHealth>();
    Scheduler scheduler;
    (void)scheduler.AddSystem("Filters", SystemAccess{}.Read<CbPosition>(),
                              [&](SystemContext& context) { context.ForEachChunk(query, [](ChunkView&) {}); });
    ASSERT_TRUE(scheduler.Build().has_value());
    g_asserts = 0;
    const interi::ScopedAssertHandler handler(&CountAssert);
    scheduler.Run(world, jobs, commands);
    EXPECT_EQ(g_asserts.load(), 1);
}

// SPDX-License-Identifier: MIT
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include "EcsTestUtil.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

using interi::ChunkView;
using interi::CommandBuffer;
using interi::CommandBuffers;
using interi::Entity;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Query;
using interi::World;
using interi::testing::CbHealth;
using interi::testing::CbPosition;
using interi::testing::CbVelocity;

namespace
{

struct Owned
{
    static inline int s_live = 0;
    explicit Owned(int v)
        : value(std::make_unique<int>(v))
    {
        ++s_live;
    }
    Owned(Owned&& other) noexcept
        : value(std::move(other.value))
    {
        ++s_live;
    }
    Owned& operator=(Owned&&) noexcept = default;
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    ~Owned() { --s_live; }
    std::unique_ptr<int> value;
};

/// Runs one "frame": every entity with odd health is destroyed, every third gets a velocity, every 50th spawns a
/// new entity, all recorded from parallel chunk iteration and played back at the end.
std::uint64_t RunFrame(std::uint32_t workers)
{
    World world;
    for (int i = 0; i < 20000; ++i)
    {
        (void)world.CreateEntity(CbPosition{static_cast<float>(i)}, CbHealth{i});
    }
    JobSystemDesc desc;
    desc.workerCount = workers;
    JobSystem jobs(desc);
    CommandBuffers commands(jobs);
    Query query;
    query.With<CbHealth>();
    for (int frame = 0; frame < 3; ++frame)
    {
        const interi::RunTicks ticks{.lastRun = 0, .thisRun = world.BeginRun()};
        query.ParallelForEachChunk(
            world, jobs, ticks,
            [&](ChunkView& view, std::size_t ordinal)
            {
                CommandBuffer& buffer = commands.Local(jobs);
                const auto health = view.Read<CbHealth>();
                const auto entities = view.Entities();
                for (std::uint32_t row = 0; row < view.Count(); ++row)
                {
                    const int value = health[row].value + frame;
                    if (value % 2 == 1)
                    {
                        buffer.DestroyEntity(ordinal, entities[row]);
                    }
                    else if (value % 3 == 0)
                    {
                        buffer.Add(ordinal, entities[row], CbVelocity{static_cast<float>(value)});
                    }
                    if (value % 50 == 0)
                    {
                        buffer.CreateEntity(ordinal, CbHealth{value + 1}, CbPosition{static_cast<float>(row)});
                    }
                }
            });
        commands.Playback(world);
    }
    return interi::testing::HashWorld(world);
}

} // namespace

TEST(CommandBuffer, Playback_AppliesInKeyOrder)
{
    World world;
    const Entity e = world.CreateEntity(CbHealth{1});
    CommandBuffers commands(2);
    commands.Slot(1).Add(5, e, CbHealth{50});
    commands.Slot(0).Add(3, e, CbHealth{30});
    commands.Slot(0).Add(7, e, CbHealth{70});
    commands.Slot(1).Add(1, e, CbHealth{10});
    commands.Playback(world);
    EXPECT_EQ(world.Get<CbHealth>(e)->value, 70); // key 7 applied last
    EXPECT_TRUE(commands.Slot(0).Empty());
    EXPECT_TRUE(commands.Slot(1).Empty());
}

TEST(CommandBuffer, CreateDestroyAddRemove_Applied)
{
    World world;
    const Entity a = world.CreateEntity(CbHealth{1}, CbPosition{});
    const Entity b = world.CreateEntity(CbHealth{2});
    CommandBuffers commands(1);
    CommandBuffer& buffer = commands.Slot(0);
    buffer.CreateEntity(0, CbHealth{3}, CbVelocity{4});
    buffer.DestroyEntity(1, b);
    buffer.Remove<CbPosition>(2, a);
    buffer.Add(3, a, CbVelocity{9});
    EXPECT_EQ(world.EntityCount(), 2u); // nothing applied yet
    commands.Playback(world);

    EXPECT_EQ(world.EntityCount(), 2u);
    EXPECT_FALSE(world.IsAlive(b));
    EXPECT_FALSE(world.Has<CbPosition>(a));
    EXPECT_EQ(world.Get<CbVelocity>(a)->x, 9.0f);
    Query created; // the new entity, plus `a`, which now has health and velocity too
    created.With<CbHealth, CbVelocity>();
    int found = 0;
    int healthSum = 0;
    created.ForEachChunk(world,
                         [&](ChunkView& view)
                         {
                             found += static_cast<int>(view.Count());
                             for (const CbHealth& health : view.Read<CbHealth>())
                             {
                                 healthSum += health.value;
                             }
                         });
    EXPECT_EQ(found, 2);
    EXPECT_EQ(healthSum, 1 + 3);
}

TEST(CommandBuffer, DoubleDestroyInOneFrame_SecondSkipped)
{
    World world;
    const Entity e = world.CreateEntity(CbHealth{1});
    CommandBuffers commands(2);
    commands.Slot(0).DestroyEntity(1, e);
    commands.Slot(1).DestroyEntity(2, e);
    commands.Slot(1).Add(3, e, CbVelocity{1}); // targets a dead entity by now
    commands.Slot(0).Remove<CbHealth>(4, e);
    commands.Playback(world);
    EXPECT_FALSE(world.IsAlive(e));
    EXPECT_EQ(world.EntityCount(), 0u);
}

TEST(CommandBuffer, PayloadsDestroyed_WhetherPlayedOrDropped)
{
    Owned::s_live = 0;
    {
        World world;
        const Entity e = world.CreateEntity(CbHealth{1});
        const Entity dead = world.CreateEntity(CbHealth{2});
        ASSERT_TRUE(world.DestroyEntity(dead));
        CommandBuffers commands(1);
        CommandBuffer& buffer = commands.Slot(0);
        for (int i = 0; i < 2000; ++i) // spans several payload blocks
        {
            buffer.CreateEntity(0, Owned{i});
        }
        buffer.Add(1, e, Owned{-1});
        buffer.Add(2, e, Owned{-2}); // assigns over the first
        buffer.Add(3, dead, Owned{-3});
        commands.Playback(world);
        EXPECT_EQ(Owned::s_live, 2001);
        EXPECT_EQ(*world.Get<Owned>(e)->value, -2);

        buffer.CreateEntity(0, Owned{7}); // recorded, never played back
        buffer.Add(0, e, Owned{8});
    }
    EXPECT_EQ(Owned::s_live, 0);
}

TEST(CommandBuffer, ConcurrentRecord_DeterministicPlayback)
{
    const std::uint64_t serial = RunFrame(0);
    EXPECT_EQ(RunFrame(3), serial);
    EXPECT_EQ(RunFrame(31), serial);
}

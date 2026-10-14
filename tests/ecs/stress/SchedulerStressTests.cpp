// SPDX-License-Identifier: MIT
#include <interi/ecs/Scheduler.h>

#include "../EcsTestUtil.h"
#include <gtest/gtest.h>

#include <cstdint>

using interi::ChunkView;
using interi::CommandBuffers;
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

// A small simulation of conflicting and independent systems, all iterating chunks in parallel, run for many frames
// at several thread counts. Under TSan, any missing ordering between conflicting systems is reported as a race;
// the world hash must also match the serial run.
TEST(Scheduler, ManyFramesMixedSystems_RaceFreeAndDeterministic)
{
    const auto simulate = [](std::uint32_t workers)
    {
        World world;
        for (int i = 0; i < 20000; ++i)
        {
            (void)world.CreateEntity(CbPosition{static_cast<float>(i)}, CbVelocity{1.0f}, CbHealth{i % 1000});
        }
        JobSystemDesc desc;
        desc.workerCount = workers;
        JobSystem jobs(desc);
        CommandBuffers commands(jobs);
        Query moving;
        moving.With<CbPosition, CbVelocity>();
        Query living;
        living.With<CbHealth>();

        Scheduler scheduler;
        (void)scheduler.AddSystem("Integrate", SystemAccess{}.Read<CbVelocity>().Write<CbPosition>(),
                                  [&](SystemContext& context)
                                  {
                                      context.ParallelForEachChunk(moving,
                                                                   [](ChunkView& view, std::size_t)
                                                                   {
                                                                       const auto velocity = view.Read<CbVelocity>();
                                                                       auto position = view.Write<CbPosition>();
                                                                       for (std::uint32_t i = 0; i < view.Count(); ++i)
                                                                       {
                                                                           position[i].x += velocity[i].x;
                                                                       }
                                                                   });
                                  });
        (void)scheduler.AddSystem("Damp", SystemAccess{}.Write<CbVelocity>(),
                                  [&](SystemContext& context)
                                  {
                                      context.ParallelForEachChunk(moving,
                                                                   [](ChunkView& view, std::size_t)
                                                                   {
                                                                       for (CbVelocity& velocity :
                                                                            view.Write<CbVelocity>())
                                                                       {
                                                                           velocity.x *= 0.99f;
                                                                       }
                                                                   });
                                  });
        (void)scheduler.AddSystem(
            "Decay", SystemAccess{}.Write<CbHealth>(),
            [&](SystemContext& context)
            {
                context.ParallelForEachChunk(
                    living,
                    [&](ChunkView& view, std::size_t ordinal)
                    {
                        auto health = view.Write<CbHealth>();
                        const auto entities = view.Entities();
                        for (std::uint32_t i = 0; i < view.Count(); ++i)
                        {
                            if (--health[i].value == 0)
                            {
                                context.Commands().DestroyEntity(context.CommandKey(ordinal), entities[i]);
                                context.Commands().CreateEntity(context.CommandKey(ordinal), CbPosition{0},
                                                                CbVelocity{2.0f}, CbHealth{500});
                            }
                        }
                    });
            });
        EXPECT_TRUE(scheduler.Build().has_value());
        for (int frame = 0; frame < 120; ++frame)
        {
            scheduler.Run(world, jobs, commands);
        }
        return interi::testing::HashWorld(world);
    };
    const std::uint64_t serial = simulate(0);
    EXPECT_EQ(simulate(3), serial);
    EXPECT_EQ(simulate(15), serial);
}

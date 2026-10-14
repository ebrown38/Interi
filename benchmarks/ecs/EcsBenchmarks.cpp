// SPDX-License-Identifier: MIT
// ECS benchmarks. The thread-count argument is the total concurrency (workers + the calling thread).
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace
{

struct Position
{
    float x, y, z;
};

struct Velocity
{
    float x, y, z;
};

struct Tag
{
};

constexpr int kEntities = 1'000'000;

interi::JobSystemDesc Threads(std::int64_t threads)
{
    interi::JobSystemDesc desc;
    desc.workerCount = static_cast<std::uint32_t>(std::max<std::int64_t>(threads, 1) - 1);
    return desc;
}

void ThreadArgs(benchmark::Benchmark* benchmark)
{
    const auto hardware = static_cast<std::int64_t>(std::max(std::thread::hardware_concurrency(), 1u));
    for (std::int64_t threads = 1; threads <= std::min<std::int64_t>(hardware, 32); threads *= 2)
    {
        benchmark->Arg(threads);
    }
}

void Populate(interi::World& world, int count)
{
    for (int i = 0; i < count; ++i)
    {
        (void)world.CreateEntity(Position{static_cast<float>(i), 0, 0}, Velocity{1, 2, 3});
    }
}

void Integrate(interi::ChunkView& view)
{
    const auto velocity = view.Read<Velocity>();
    auto position = view.Write<Position>();
    for (std::uint32_t i = 0; i < view.Count(); ++i)
    {
        position[i].x += velocity[i].x * 0.016f;
        position[i].y += velocity[i].y * 0.016f;
        position[i].z += velocity[i].z * 0.016f;
    }
}

/// Serial chunk iteration over 1M entities (position += velocity * dt).
void BM_QueryIterate_Serial(benchmark::State& state)
{
    interi::World world;
    Populate(world, kEntities);
    interi::Query query;
    query.With<Position, Velocity>();
    for (auto _ : state)
    {
        query.ForEachChunk(world, [](interi::ChunkView& view) { Integrate(view); });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kEntities);
}
BENCHMARK(BM_QueryIterate_Serial)->Unit(benchmark::kMicrosecond);

/// The same iteration with ParallelForEachChunk, by thread count.
void BM_QueryIterate_Parallel(benchmark::State& state)
{
    interi::World world;
    Populate(world, kEntities);
    interi::JobSystem jobs(Threads(state.range(0)));
    interi::Query query;
    query.With<Position, Velocity>();
    for (auto _ : state)
    {
        const interi::RunTicks ticks{.lastRun = 0, .thisRun = world.BeginRun()};
        query.ParallelForEachChunk(world, jobs, ticks, [](interi::ChunkView& view, std::size_t) { Integrate(view); });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kEntities);
}
BENCHMARK(BM_QueryIterate_Parallel)->Apply(ThreadArgs)->Unit(benchmark::kMicrosecond)->UseRealTime();

/// Adding then removing a tag on 10k entities: two archetype moves per entity.
void BM_AddRemoveChurn(benchmark::State& state)
{
    constexpr int kChurn = 10'000;
    interi::World world;
    std::vector<interi::Entity> entities;
    entities.reserve(kChurn);
    for (int i = 0; i < kChurn; ++i)
    {
        entities.push_back(world.CreateEntity(Position{}, Velocity{}));
    }
    for (auto _ : state)
    {
        for (const interi::Entity entity : entities)
        {
            (void)world.Add(entity, Tag{});
        }
        for (const interi::Entity entity : entities)
        {
            (void)world.Remove<Tag>(entity);
        }
    }
    state.SetItemsProcessed(state.iterations() * kChurn * 2);
}
BENCHMARK(BM_AddRemoveChurn)->Unit(benchmark::kMicrosecond);

/// Recording 100k destroy + create commands across threads, then playing them back.
void BM_CommandBufferPlayback(benchmark::State& state)
{
    constexpr int kCount = 100'000;
    interi::JobSystem jobs(Threads(state.range(0)));
    interi::CommandBuffers commands(jobs);
    interi::Query query;
    query.With<Position>();
    for (auto _ : state)
    {
        state.PauseTiming();
        auto world = std::make_unique<interi::World>();
        Populate(*world, kCount);
        state.ResumeTiming();
        const interi::RunTicks ticks{.lastRun = 0, .thisRun = world->BeginRun()};
        query.ParallelForEachChunk(*world, jobs, ticks,
                                   [&](interi::ChunkView& view, std::size_t ordinal)
                                   {
                                       interi::CommandBuffer& buffer = commands.Local(jobs);
                                       for (const interi::Entity entity : view.Entities())
                                       {
                                           buffer.DestroyEntity(ordinal, entity);
                                           buffer.CreateEntity(ordinal, Position{}, Tag{});
                                       }
                                   });
        commands.Playback(*world);
        benchmark::DoNotOptimize(world->EntityCount());
        state.PauseTiming();
        world.reset(); // destruction is not part of the measurement
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * kCount * 2);
}
BENCHMARK(BM_CommandBufferPlayback)->Apply(ThreadArgs)->Unit(benchmark::kMillisecond)->UseRealTime();

} // namespace

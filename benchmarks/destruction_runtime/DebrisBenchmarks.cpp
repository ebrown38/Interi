// SPDX-License-Identifier: MIT
// Debris capacity with 7 workers (M8.4): frame cost of many active debris bodies (physics step + DebrisSystem
// Update), of many particles, and of churn at the cap (eviction plus body creation every frame).
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/PhysicsSync.h>

#include "../../tests/physics/PhysicsTestUtil.h"
#include "BenchmarkJolt.h"
#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::physics;

constexpr float kStep = 1.0f / 60.0f;

using interi::benchmarks::EnsureJolt;

double Milliseconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

/// Physics, ECS and debris with 7 workers, a ground and a few shared box shapes (debris shares hull shapes per
/// asset the same way).
struct DebrisBench
{
    explicit DebrisBench(const DebrisSettings& settings)
        : jobs(physics::testing::Workers(7))
        , physicsWorld(jobs)
        , sync(physicsWorld)
        , debris(physicsWorld, settings)
    {
        physics::testing::AddGround(physicsWorld);
        for (std::size_t i = 0; i < shapes.size(); ++i)
        {
            shapes[i] = ColliderComponent::Box(Vec3(0.1f + 0.05f * static_cast<float>(i)));
        }
    }

    /// Adopts a box of the i-th size at `position`, falling and spinning a little.
    void Spawn(std::uint32_t i, const Vec3& position)
    {
        Transform pose;
        pose.position = position;
        RigidBodyComponent body;
        body.layer = ObjectLayer::Debris;
        body.linearVelocity = Vec3(0.0f, -1.0f, 0.0f);
        body.angularVelocity = Vec3(0.3f, 0.0f, 0.2f);
        const float half = 0.1f + 0.05f * static_cast<float>(i % shapes.size());
        (void)debris.Adopt(world, world.CreateEntity(pose, body, shapes[i % shapes.size()]), 8.0f * half * half * half);
    }

    /// `count` boxes in a square column above the ground, 0.8 m apart.
    void SpawnColumn(std::uint32_t count, float baseHeight)
    {
        const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(count) / 8.0)));
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::uint32_t layer = i / (side * side);
            const std::uint32_t inLayer = i % (side * side);
            Spawn(i, Vec3(0.8f * static_cast<float>(inLayer % side), baseHeight + 0.8f * static_cast<float>(layer),
                          0.8f * static_cast<float>(inLayer / side)));
        }
    }

    JobSystem jobs;
    PhysicsWorld physicsWorld;
    PhysicsSync sync;
    World world;
    DebrisSystem debris;
    std::array<ColliderComponent, 4> shapes;
};

DebrisSettings BodySettings(std::uint32_t maxBodies)
{
    DebrisSettings settings;
    settings.maxBodies = maxBodies;
    settings.mergeRubble = false;
    settings.policy.fadeMaxVolume = 0.0f;
    return settings;
}

/// Arg: debris body count. A column of boxes falls and piles up; per-frame physics step and debris update.
void BM_DebrisBodies(benchmark::State& state)
{
    EnsureJolt();
    constexpr int kFrames = 120;
    const auto count = static_cast<std::uint32_t>(state.range(0));
    double worstFrame = 0.0;
    double totalPhysics = 0.0;
    double totalDebris = 0.0;
    std::unique_ptr<DebrisBench> current;
    for (auto _ : state)
    {
        state.PauseTiming();
        current.reset();
        current = std::make_unique<DebrisBench>(BodySettings(count));
        DebrisBench& bench = *current;
        bench.SpawnColumn(count, 1.0f);
        bench.sync.Step(bench.world, bench.jobs, kStep); // creates the bodies
        bench.physicsWorld.OptimizeBroadPhase();
        state.ResumeTiming();
        for (int frame = 0; frame < kFrames; ++frame)
        {
            const auto start = std::chrono::steady_clock::now();
            bench.sync.Step(bench.world, bench.jobs, kStep);
            const auto stepped = std::chrono::steady_clock::now();
            bench.debris.Update(bench.world, bench.jobs, {.position = Vec3(0.0f, 2.0f, -20.0f), .cullPlanes = {}},
                                kStep);
            const auto updated = std::chrono::steady_clock::now();
            worstFrame = std::max(worstFrame, Milliseconds(updated - start));
            totalPhysics += Milliseconds(stepped - start);
            totalDebris += Milliseconds(updated - stepped);
        }
    }
    current.reset();
    const auto frames = static_cast<double>(state.iterations() * kFrames);
    state.counters["bodies"] = static_cast<double>(count);
    state.counters["worst_frame_ms"] = worstFrame;
    state.counters["mean_physics_ms"] = totalPhysics / frames;
    state.counters["mean_debris_ms"] = totalDebris / frames;
}
BENCHMARK(BM_DebrisBodies)->Arg(500)->Arg(1000)->Arg(2000)->Arg(4000)->Iterations(2)->Unit(benchmark::kMillisecond);

/// Arg: particle count. Every piece is converted to a particle at once (far from the view) before it gets a body;
/// then the per-frame DebrisSystem update (integration, ages, distances, priorities).
void BM_DebrisParticles(benchmark::State& state)
{
    EnsureJolt();
    const auto count = static_cast<std::uint32_t>(state.range(0));
    DebrisSettings settings = BodySettings(count);
    settings.maxParticles = count;
    settings.policy.particleMaxVolume = 1.0f;
    settings.policy.particleDistance = 0.0f;
    settings.policy.particleLifetime = 1.0e6f;
    DebrisBench bench(settings);
    bench.SpawnColumn(count, 100.0f);
    const DebrisView view{.position = Vec3(0.0f, 0.0f, -50.0f), .cullPlanes = {}};
    bench.debris.Update(bench.world, bench.jobs, view, kStep);
    if (bench.debris.ParticleCount() != count)
    {
        state.SkipWithError("not every piece became a particle");
        return;
    }
    for (auto _ : state)
    {
        bench.debris.Update(bench.world, bench.jobs, view, kStep);
    }
    state.counters["particles"] = static_cast<double>(count);
}
BENCHMARK(BM_DebrisParticles)->Arg(10'000)->Arg(50'000)->Arg(100'000)->Unit(benchmark::kMillisecond);

/// Args: {cap, new pieces per frame}. The pool is full; every frame adopts new pieces, evicting as many old ones,
/// so PhysicsSync destroys and creates that many bodies. Measures whether Jolt body recycling would pay off.
void BM_DebrisChurn(benchmark::State& state)
{
    EnsureJolt();
    constexpr int kFrames = 60;
    const auto cap = static_cast<std::uint32_t>(state.range(0));
    const auto perFrame = static_cast<std::uint32_t>(state.range(1));
    double totalAdopt = 0.0;
    double totalPhysics = 0.0;
    std::unique_ptr<DebrisBench> current;
    for (auto _ : state)
    {
        state.PauseTiming();
        current.reset();
        current = std::make_unique<DebrisBench>(BodySettings(cap));
        DebrisBench& bench = *current;
        bench.SpawnColumn(cap, 1.0f);
        bench.sync.Step(bench.world, bench.jobs, kStep);
        bench.physicsWorld.OptimizeBroadPhase();
        std::uint32_t next = 0;
        state.ResumeTiming();
        for (int frame = 0; frame < kFrames; ++frame)
        {
            const auto start = std::chrono::steady_clock::now();
            bench.debris.Update(bench.world, bench.jobs, {.position = Vec3(), .cullPlanes = {}}, kStep);
            for (std::uint32_t i = 0; i < perFrame; ++i, ++next)
            {
                bench.Spawn(next, Vec3(-20.0f + 0.8f * static_cast<float>(next % 50), 20.0f,
                                       -20.0f + 0.8f * static_cast<float>((next / 50) % 50)));
            }
            const auto adopted = std::chrono::steady_clock::now();
            bench.sync.Step(bench.world, bench.jobs, kStep);
            const auto stepped = std::chrono::steady_clock::now();
            totalAdopt += Milliseconds(adopted - start);
            totalPhysics += Milliseconds(stepped - adopted);
        }
    }
    current.reset();
    const auto frames = static_cast<double>(state.iterations() * kFrames);
    state.counters["mean_update_adopt_ms"] = totalAdopt / frames;
    state.counters["mean_physics_ms"] = totalPhysics / frames;
}
BENCHMARK(BM_DebrisChurn)
    ->Args({1000, 0})
    ->Args({1000, 50})
    ->Args({1000, 200})
    ->Iterations(2)
    ->Unit(benchmark::kMillisecond);

} // namespace

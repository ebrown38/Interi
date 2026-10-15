// SPDX-License-Identifier: MIT
// Frame cost of a whole-building collapse with 7 workers: destruction Update (damage, stress, islands, pieces) and
// the physics step, worst and mean over the frames after the ground-floor columns are destroyed.
#include <interi/physics/JoltRuntime.h>

#include "../../tests/destruction_runtime/ScenarioUtil.h"
#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

void EnsureJolt()
{
    static const physics::JoltRuntime runtime; // lives until exit, after every benchmark's PhysicsWorld
}

/// `size` x `size` floors on columns every 3 cells, `stories` storeys of 2 m columns plus a 1 m slab.
std::vector<Cell> Building(int size, int stories)
{
    std::vector<Cell> cells;
    for (int story = 0; story < stories; ++story)
    {
        const int base = story * 3;
        for (int y = base; y < base + 2; ++y)
        {
            for (int x = 0; x < size; x += 3)
            {
                for (int z = 0; z < size; z += 3)
                {
                    cells.push_back({x, y, z});
                }
            }
        }
        for (int z = 0; z < size; ++z)
        {
            for (int x = 0; x < size; ++x)
            {
                cells.push_back({x, base + 2, z});
            }
        }
    }
    return cells;
}

double Milliseconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

/// Args: {floor size, storeys}.
void BM_BuildingCollapse(benchmark::State& state)
{
    EnsureJolt();
    constexpr int kFrames = 120;
    const std::vector<Cell> cells = Building(static_cast<int>(state.range(0)), static_cast<int>(state.range(1)));
    const DestructibleAsset asset = WithConcreteLimits(MakeGridAsset(cells, Concrete()));
    double worstDestruction = 0.0;
    double worstPhysics = 0.0;
    double totalDestruction = 0.0;
    std::unique_ptr<Scenario> current;
    for (auto _ : state)
    {
        state.PauseTiming(); // set-up and the previous run's tear-down are not part of a frame
        current.reset();
        current = std::make_unique<Scenario>(7);
        Scenario& scenario = *current;
        (void)scenario.Spawn(asset);
        scenario.Step();
        scenario.physicsWorld.OptimizeBroadPhase();
        for (const Cell& cell : cells)
        {
            if (cell[1] == 1) // every ground-floor column's upper chunk
            {
                const Vec3 center(static_cast<float>(cell[0]) + 0.5f, 1.5f, static_cast<float>(cell[2]) + 0.5f);
                scenario.destruction.QueueDamage(
                    {.center = center + Vec3(0.8f, 0.0f, 0.8f), .energy = 1.0e6f, .innerRadius = 1.3f, .radius = 1.5f},
                    DamageKind::Explosion);
            }
        }
        state.ResumeTiming();
        for (int frame = 0; frame < kFrames; ++frame)
        {
            const auto start = std::chrono::steady_clock::now();
            scenario.sync.Step(scenario.world, scenario.jobs, kScenarioStep);
            const auto stepped = std::chrono::steady_clock::now();
            scenario.destruction.Update(scenario.world, scenario.jobs);
            const auto updated = std::chrono::steady_clock::now();
            worstPhysics = std::max(worstPhysics, Milliseconds(stepped - start));
            worstDestruction = std::max(worstDestruction, Milliseconds(updated - stepped));
            totalDestruction += Milliseconds(updated - stepped);
        }
    }
    current.reset(); // after the timed loop
    state.counters["chunks"] = static_cast<double>(asset.chunks.size());
    state.counters["bonds"] = static_cast<double>(asset.bonds.size());
    state.counters["worst_destruction_ms"] = worstDestruction;
    state.counters["worst_physics_ms"] = worstPhysics;
    state.counters["mean_destruction_ms"] =
        totalDestruction / static_cast<double>(state.iterations() * static_cast<std::int64_t>(kFrames));
}
BENCHMARK(BM_BuildingCollapse)->Args({10, 2})->Args({19, 4})->Iterations(3)->Unit(benchmark::kMillisecond);

} // namespace

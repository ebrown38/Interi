// SPDX-License-Identifier: MIT
// Runtime destruction graph work (frame budget): stress solves at 1k / 10k / 50k bonds and island labelling, at 0
// and 7 workers (the console proxy: ~7 cores free for the game).
#include <interi/destruction/Islands.h>
#include <interi/destruction/StressSolver.h>
#include <interi/jobs/JobSystem.h>

#include "../../tests/destruction/StressTestUtil.h"
#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

JobSystemDesc Workers(std::int64_t count)
{
    JobSystemDesc desc;
    desc.workerCount = static_cast<std::uint32_t>(count);
    return desc;
}

/// A wall of unit cubes standing on the ground with about `bonds` bonds.
StressGraph Wall(std::int64_t bonds)
{
    switch (bonds)
    {
    case 1000:
        return MakeGrid(BoxCells(12, 12, 3)).graph; // 1080 bonds
    case 10000:
        return MakeGrid(BoxCells(30, 30, 4)).graph; // 9660 bonds
    default:
        return MakeGrid(BoxCells(60, 60, 5)).graph; // 49800 bonds
    }
}

/// A full solve from rest to the default tolerance (the worst case after a big change). Args: {bonds, workers}.
void BM_StressSolveCold(benchmark::State& state)
{
    JobSystem jobs(Workers(state.range(1)));
    StressSolver solver(Wall(state.range(0)));
    (void)solver.Solve(jobs, 1); // builds the support set and preconditioner once
    std::uint32_t iterations = 0;
    for (auto _ : state)
    {
        solver.ResetSolution();
        iterations = solver.Solve(jobs, 1'000'000).iterations;
    }
    state.counters["bonds"] = static_cast<double>(solver.BondCount());
    state.counters["iterations"] = iterations;
}
BENCHMARK(BM_StressSolveCold)->ArgsProduct({{1000, 10000, 50000}, {0, 7}})->Unit(benchmark::kMillisecond);

/// Cost of one CG iteration per bond: what DestructionSettings::stressWorkBudget is measured in. Args: as above.
void BM_StressBondIteration(benchmark::State& state)
{
    constexpr std::uint32_t kIterations = 20;
    JobSystem jobs(Workers(state.range(1)));
    StressSolver solver(Wall(state.range(0)));
    (void)solver.Solve(jobs, 1);
    for (auto _ : state)
    {
        solver.ResetSolution();
        benchmark::DoNotOptimize(solver.Solve(jobs, kIterations));
    }
    state.SetItemsProcessed(state.iterations() * kIterations * static_cast<std::int64_t>(solver.BondCount()));
}
BENCHMARK(BM_StressBondIteration)->ArgsProduct({{1000, 10000, 50000}, {0, 7}})->Unit(benchmark::kMicrosecond);

/// Island labelling after damage: the 50k-bond wall with every 7th bond broken. Args: {workers}.
void BM_LabelComponents(benchmark::State& state)
{
    JobSystem jobs(Workers(state.range(0)));
    const StressGraph wall = Wall(50000);
    std::vector<GraphEdge> edges;
    std::vector<std::uint8_t> broken;
    for (std::size_t i = 0; i < wall.bonds.size(); ++i)
    {
        edges.emplace_back(wall.bonds[i].nodeA, wall.bonds[i].nodeB);
        broken.push_back(i % 7 == 0 ? 1 : 0);
    }
    std::vector<std::uint32_t> labels(wall.nodes.size());
    for (auto _ : state)
    {
        LabelComponents(jobs, static_cast<std::uint32_t>(labels.size()), edges, broken, labels);
        benchmark::DoNotOptimize(labels.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(edges.size()));
}
BENCHMARK(BM_LabelComponents)->Arg(0)->Arg(7)->Unit(benchmark::kMicrosecond);

} // namespace

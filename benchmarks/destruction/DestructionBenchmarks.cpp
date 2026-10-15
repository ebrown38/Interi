// SPDX-License-Identifier: MIT
// Offline fracture cost (cook times, not frame budget): Voronoi fracture of a wall at 1 and 7 workers, plane cuts,
// and the bond search over a fractured building.
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/Voronoi.h>
#include <interi/jobs/JobSystem.h>

#include <benchmark/benchmark.h>

#include <cstdint>

namespace
{

using namespace interi;
using namespace interi::destruction;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

FractureMesh Wall()
{
    return MakeBoxMesh({0, 0, 0}, {6, 3, 0.3f});
}

std::vector<Vec3> Seeds(const FractureMesh& mesh, std::uint32_t count)
{
    SeedSettings settings;
    settings.count = count;
    return GenerateSeeds(mesh, settings);
}

/// Args: {cells, workers}.
void BM_VoronoiFracture(benchmark::State& state)
{
    JobSystem jobs(Workers(static_cast<std::uint32_t>(state.range(1))));
    const FractureMesh wall = Wall();
    const std::vector<Vec3> seeds = Seeds(wall, static_cast<std::uint32_t>(state.range(0)));
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(VoronoiFracture(wall, seeds, {}, jobs));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_VoronoiFracture)
    ->Args({64, 0})
    ->Args({64, 7})
    ->Args({512, 0})
    ->Args({512, 7})
    ->Unit(benchmark::kMillisecond);

void BM_CutMesh(benchmark::State& state)
{
    JobSystem jobs(Workers(0));
    // A fractured wall merged into one mesh: many triangles crossing the plane.
    FractureMesh merged;
    for (FracturePiece& piece : VoronoiFracture(Wall(), Seeds(Wall(), 64), {}, jobs).pieces)
    {
        AppendMesh(merged, piece.mesh);
    }
    const CutPlane plane = CutPlane::FromPointNormal({3.01, 1.49, 0.15}, {0.6, 0.8, 0.1});
    const double tolerance = CutTolerance(merged);
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(CutMesh(merged, plane, tolerance));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(merged.triangles.size()));
}
BENCHMARK(BM_CutMesh)->Unit(benchmark::kMicrosecond);

/// Arg: pieces per object (6 objects).
void BM_BuildBondGraph(benchmark::State& state)
{
    JobSystem jobs(Workers(7));
    const std::vector<std::pair<Vec3, Vec3>> parts{
        {{0, 0, 0}, {6, 0.3f, 6}},
        {{0, 0.3f, 0}, {6, 3.3f, 0.3f}},
        {{0, 0.3f, 5.7f}, {6, 3.3f, 6}},
        {{0, 0.3f, 0.3f}, {0.3f, 3.3f, 5.7f}},
        {{5.7f, 0.3f, 0.3f}, {6, 3.3f, 5.7f}},
        {{0, 3.3f, 0}, {6, 3.6f, 6}},
    };
    std::vector<Chunk> chunks;
    for (const auto& [min, max] : parts)
    {
        const FractureMesh mesh = MakeBoxMesh(min, max);
        for (FracturePiece& piece :
             VoronoiFracture(mesh, Seeds(mesh, static_cast<std::uint32_t>(state.range(0))), {}, jobs).pieces)
        {
            Chunk chunk;
            chunk.mesh = std::move(piece.mesh);
            chunks.push_back(std::move(chunk));
        }
    }
    const std::vector<DestructionMaterial> materials{DestructionMaterial{.name = "concrete"}};
    BondSettings settings;
    settings.groundHeight = 0.0f;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(BuildBondGraph(chunks, materials, settings));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(chunks.size()));
}
BENCHMARK(BM_BuildBondGraph)->Arg(16)->Arg(128)->Unit(benchmark::kMillisecond);

} // namespace

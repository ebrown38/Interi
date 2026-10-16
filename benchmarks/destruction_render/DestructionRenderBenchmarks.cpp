// SPDX-License-Identifier: MIT
// CPU cost of drawing destruction: picking the chunk meshes of a structure's pieces (ChunkRenderSet::Cover, the core
// of ChunkGatherer) and turning break events into particles.
#include <interi/destruction/ChunkRenderSet.h>
#include <interi/destruction_render/BreakEffects.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;

/// A breadth-first hierarchy of one object: the root, `regions` regions, `leaves` leaves per region.
std::vector<AssetChunk> Hierarchy(std::uint32_t regions, std::uint32_t leaves)
{
    std::vector<AssetChunk> chunks(1 + regions + regions * leaves);
    chunks[0].firstChild = 1;
    chunks[0].childCount = regions;
    for (std::uint32_t r = 0; r < regions; ++r)
    {
        AssetChunk& region = chunks[1 + r];
        region.parent = 0;
        region.firstChild = 1 + regions + r * leaves;
        region.childCount = leaves;
        for (std::uint32_t l = 0; l < leaves; ++l)
        {
            chunks[region.firstChild + l].parent = 1 + r;
        }
    }
    return chunks;
}

/// Covers every leaf of a 64-region x 32-leaf structure (2048 leaves). Arg 0: intact (one piece); arg 1: every
/// region lost one leaf (64 small pieces split off, so 64 regions fall back to their leaves).
void BM_ChunkRenderSetCover(benchmark::State& state)
{
    constexpr std::uint32_t kRegions = 64;
    constexpr std::uint32_t kLeaves = 32;
    const std::vector<AssetChunk> chunks = Hierarchy(kRegions, kLeaves);
    ChunkRenderSet set(chunks);
    std::vector<std::vector<std::uint32_t>> pieces(1);
    for (std::uint32_t c = 1 + kRegions; c < chunks.size(); ++c)
    {
        const bool splitOff = state.range(0) == 1 && (c - 1 - kRegions) % kLeaves == 0;
        if (splitOff)
        {
            pieces.push_back({c});
        }
        else
        {
            pieces[0].push_back(c);
        }
    }
    std::vector<std::uint32_t> out;
    for (auto _ : state)
    {
        out.clear();
        for (const std::vector<std::uint32_t>& piece : pieces)
        {
            set.Cover(piece, out);
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.counters["meshes"] = static_cast<double>(out.size());
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kRegions * kLeaves));
}
BENCHMARK(BM_ChunkRenderSetCover)->Arg(0)->Arg(1);

/// Particles for `range(0)` concrete bond breaks (0.25 m^2 each) per call.
void BM_SpawnBreakParticles(benchmark::State& state)
{
    const BreakEffectTable table = DefaultBreakEffects();
    BreakEvent event;
    event.normal = {0.0f, 0.0f, 1.0f};
    event.area = 0.25f;
    std::vector<render::Particle> out;
    for (auto _ : state)
    {
        out.clear();
        for (std::int64_t i = 0; i < state.range(0); ++i)
        {
            event.position = {static_cast<float>(i) * 0.1f, 1.0f, 0.0f};
            SpawnBreakParticles(event, table[0], static_cast<std::uint64_t>(i), out);
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.counters["particles"] = static_cast<double>(out.size());
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(out.size()));
}
BENCHMARK(BM_SpawnBreakParticles)->Arg(10)->Arg(100)->Arg(1000);

} // namespace

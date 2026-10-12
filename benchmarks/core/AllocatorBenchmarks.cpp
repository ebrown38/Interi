// SPDX-License-Identifier: MIT
#include <interi/core/memory/HeapAllocator.h>
#include <interi/core/memory/LinearAllocator.h>
#include <interi/core/memory/PoolAllocator.h>
#include <interi/core/memory/TlsfAllocator.h>

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>

namespace
{
constexpr std::size_t kBatch = 256;
constexpr std::size_t kBlockSize = 64;

/// Allocates a batch of equal-sized blocks and frees them in reverse: the debris-chunk pattern.
void AllocFreeBatch(benchmark::State& state, interi::IAllocator& allocator)
{
    std::array<void*, kBatch> blocks{};
    for (auto _ : state)
    {
        for (void*& block : blocks)
        {
            block = allocator.Allocate(kBlockSize, 16);
        }
        benchmark::DoNotOptimize(blocks.data());
        for (std::size_t i = kBatch; i-- > 0;)
        {
            allocator.Free(blocks[i], kBlockSize);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
}

void BM_HeapAllocator_AllocFree(benchmark::State& state)
{
    AllocFreeBatch(state, interi::GetDefaultAllocator());
}
BENCHMARK(BM_HeapAllocator_AllocFree);

void BM_PoolAllocator_AllocFree(benchmark::State& state)
{
    interi::PoolAllocator pool(kBlockSize, kBatch);
    AllocFreeBatch(state, pool);
}
BENCHMARK(BM_PoolAllocator_AllocFree);

void BM_TlsfAllocator_AllocFree(benchmark::State& state)
{
    interi::TlsfAllocator tlsf(1 << 20);
    AllocFreeBatch(state, tlsf);
}
BENCHMARK(BM_TlsfAllocator_AllocFree);

void BM_LinearAllocator_AllocReset(benchmark::State& state)
{
    interi::LinearAllocator linear(kBatch * kBlockSize);
    for (auto _ : state)
    {
        for (std::size_t i = 0; i < kBatch; ++i)
        {
            benchmark::DoNotOptimize(linear.Allocate(kBlockSize, 16));
        }
        linear.Reset();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
}
BENCHMARK(BM_LinearAllocator_AllocReset);
} // namespace

// SPDX-License-Identifier: MIT
#include <interi/core/containers/HashMap.h>
#include <interi/core/containers/MPMCQueue.h>

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
std::vector<std::uint64_t> MakeKeys(std::size_t count)
{
    std::vector<std::uint64_t> keys(count);
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (auto& key : keys)
    {
        state = interi::MixBits(state + 1);
        key = state;
    }
    return keys;
}

template <typename Map>
void Lookup(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    const std::vector<std::uint64_t> keys = MakeKeys(count);
    Map map;
    for (std::uint64_t key : keys)
    {
        map[key] = key;
    }
    for (auto _ : state)
    {
        std::uint64_t sum = 0;
        for (std::uint64_t key : keys)
        {
            if constexpr (requires { map.Find(key); })
            {
                sum += *map.Find(key);
            }
            else
            {
                sum += map.find(key)->second;
            }
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * state.range(0));
}

void BM_HashMap_Lookup(benchmark::State& state)
{
    Lookup<interi::HashMap<std::uint64_t, std::uint64_t>>(state);
}
BENCHMARK(BM_HashMap_Lookup)->Arg(1 << 10)->Arg(1 << 16);

void BM_StdUnorderedMap_Lookup(benchmark::State& state)
{
    Lookup<std::unordered_map<std::uint64_t, std::uint64_t>>(state);
}
BENCHMARK(BM_StdUnorderedMap_Lookup)->Arg(1 << 10)->Arg(1 << 16);

/// One producer and one consumer per pair of threads, all sharing one queue.
void BM_MPMCQueue_Throughput(benchmark::State& state)
{
    static interi::MPMCQueue<std::uint64_t>* queue = nullptr;
    constexpr int kItemsPerThread = 10'000;
    if (state.thread_index() == 0)
    {
        queue = new interi::MPMCQueue<std::uint64_t>(1024);
    }
    // Benchmark threads start together after setup, so the queue exists before the loop runs.
    for (auto _ : state)
    {
        const bool producer = state.thread_index() % 2 == 0;
        for (int i = 0; i < kItemsPerThread; ++i)
        {
            if (producer)
            {
                while (!queue->TryPush(static_cast<std::uint64_t>(i)))
                {
                    std::this_thread::yield();
                }
            }
            else
            {
                std::uint64_t item = 0;
                while (!queue->TryPop(item))
                {
                    std::this_thread::yield();
                }
                benchmark::DoNotOptimize(item);
            }
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * kItemsPerThread);
    if (state.thread_index() == 0)
    {
        delete queue;
        queue = nullptr;
    }
}
BENCHMARK(BM_MPMCQueue_Throughput)->ThreadRange(2, 8)->UseRealTime();
} // namespace

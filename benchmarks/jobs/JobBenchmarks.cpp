// SPDX-License-Identifier: MIT
// Job system benchmarks. The thread-count argument is the total concurrency (workers + the calling thread).
#include <interi/core/Hash.h>
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace
{

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

std::vector<std::uint32_t> MakeKeys(std::size_t count)
{
    std::vector<std::uint32_t> keys(count);
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    for (std::uint32_t& key : keys)
    {
        state = interi::MixBits(state + 1);
        key = static_cast<std::uint32_t>(state);
    }
    return keys;
}

/// Throughput of submitting a batch of empty jobs from the main thread and waiting for them: the per-job cost of
/// Submit + scheduling + Execute (target < 1 µs).
void JobSystem_SubmitBatchAndWait(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    constexpr int kBatch = 1024;
    for (auto _ : state)
    {
        interi::JobCounter counter;
        for (int i = 0; i < kBatch; ++i)
        {
            jobs.Submit([] {}, &counter);
        }
        jobs.WaitFor(counter);
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(JobSystem_SubmitBatchAndWait)->Apply(ThreadArgs)->UseRealTime();

/// Latency of one Submit followed by WaitFor (round trip through the scheduler).
void JobSystem_SubmitOneAndWait(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    for (auto _ : state)
    {
        interi::JobCounter counter;
        jobs.Submit([] {}, &counter);
        jobs.WaitFor(counter);
    }
}
BENCHMARK(JobSystem_SubmitOneAndWait)->Arg(1)->Arg(8)->UseRealTime();

/// Jobs submitting jobs from workers (deque push/pop/steal path rather than the injection queue).
void JobSystem_NestedSubmit(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    constexpr int kParents = 64;
    constexpr int kChildren = 64;
    for (auto _ : state)
    {
        interi::JobCounter counter;
        for (int p = 0; p < kParents; ++p)
        {
            jobs.Submit(
                [&jobs, &counter]
                {
                    for (int c = 0; c < kChildren; ++c)
                    {
                        jobs.Submit([] {}, &counter);
                    }
                },
                &counter);
        }
        jobs.WaitFor(counter);
    }
    state.SetItemsProcessed(state.iterations() * kParents * (kChildren + 1));
}
BENCHMARK(JobSystem_NestedSubmit)->Apply(ThreadArgs)->UseRealTime();

/// Roughly `iterations` dependent multiply-adds; stands in for per-element simulation work.
float Work(float value, int iterations)
{
    for (int i = 0; i < iterations; ++i)
    {
        value = value * 0.999f + 0.5f;
    }
    return value;
}

/// ParallelFor scaling on compute-bound work (64K elements x 256 dependent ops, ~64 elements per chunk).
void ParallelFor_ComputeScaling(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    constexpr std::size_t kCount = std::size_t{64} * 1024;
    std::vector<float> values(kCount, 1.5f);
    for (auto _ : state)
    {
        interi::ParallelFor(jobs, 0, kCount, 64, [&values](std::size_t i) { values[i] = Work(values[i], 256); });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kCount));
}
BENCHMARK(ParallelFor_ComputeScaling)->Apply(ThreadArgs)->UseRealTime()->Unit(benchmark::kMicrosecond);

/// ParallelFor scaling on a memory-bound loop over 4M floats (saturates DRAM bandwidth well before 32 threads).
void ParallelFor_MemoryScaling(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    constexpr std::size_t kCount = std::size_t{4} << 20;
    std::vector<float> values(kCount, 1.5f);
    for (auto _ : state)
    {
        interi::ParallelFor(jobs, 0, kCount, 4096, [&values](std::size_t i) { values[i] = values[i] * 0.5f + 1.0f; });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kCount));
}
BENCHMARK(ParallelFor_MemoryScaling)->Apply(ThreadArgs)->UseRealTime()->Unit(benchmark::kMicrosecond);

/// Many independent small jobs (~1 µs of work each) submitted from the main thread.
void JobSystem_SmallJobsScaling(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    constexpr int kJobs = 4096;
    std::vector<float> results(kJobs);
    for (auto _ : state)
    {
        interi::JobCounter counter;
        for (int i = 0; i < kJobs; ++i)
        {
            jobs.Submit([&results, i] { results[static_cast<std::size_t>(i)] = Work(static_cast<float>(i), 1000); },
                        &counter);
        }
        jobs.WaitFor(counter);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kJobs);
}
BENCHMARK(JobSystem_SmallJobsScaling)->Apply(ThreadArgs)->UseRealTime()->Unit(benchmark::kMicrosecond);

void ParallelRadixSort_U32(benchmark::State& state)
{
    interi::JobSystem jobs(Threads(state.range(0)));
    const std::vector<std::uint32_t> input = MakeKeys(std::size_t{1} << 22);
    std::vector<std::uint32_t> keys(input.size());
    std::vector<std::uint32_t> scratch(input.size());
    for (auto _ : state)
    {
        state.PauseTiming();
        keys = input;
        state.ResumeTiming();
        interi::ParallelRadixSort(jobs, std::span<std::uint32_t>(keys), std::span<std::uint32_t>(scratch));
        benchmark::DoNotOptimize(keys.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}
BENCHMARK(ParallelRadixSort_U32)->Apply(ThreadArgs)->UseRealTime()->Unit(benchmark::kMillisecond);

void StdSort_U32(benchmark::State& state)
{
    const std::vector<std::uint32_t> input = MakeKeys(std::size_t{1} << 22);
    std::vector<std::uint32_t> keys(input.size());
    for (auto _ : state)
    {
        state.PauseTiming();
        keys = input;
        state.ResumeTiming();
        std::sort(keys.begin(), keys.end());
        benchmark::DoNotOptimize(keys.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(input.size()));
}
BENCHMARK(StdSort_U32)->Unit(benchmark::kMillisecond);

} // namespace

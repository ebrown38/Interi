// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/jobs/JobSystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

/// Data-parallel algorithms on a JobSystem.
///
/// Determinism: a range is always cut into the same chunks, [begin + k*grain, begin + (k+1)*grain), whatever the
/// thread count. Reductions and scans fold within a chunk left to right and then combine chunk results in chunk
/// order, so results (including floating point) are bit-identical at any thread count. They can still differ from
/// a plain serial fold, because floating-point addition is not associative.
///
/// Callbacks run concurrently on several threads and must not throw. The calling thread takes part, and the call
/// returns once all work is done.
namespace interi
{

/// Calls `function(chunkBegin, chunkEnd)` once per chunk of [begin, end). A grain of 0 is treated as 1.
template <typename F>
void ParallelForChunks(JobSystem& jobs, std::size_t begin, std::size_t end, std::size_t grain, F&& function,
                       JobPriority priority = JobPriority::Normal)
{
    if (begin >= end)
    {
        return;
    }
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t count = end - begin;
    const std::size_t chunkCount = count / grain + (count % grain != 0 ? 1 : 0);
    if (chunkCount == 1)
    {
        function(begin, end);
        return;
    }

    // Each participating thread claims chunks from a shared cursor until none are left.
    std::atomic<std::size_t> nextChunk{0};
    const auto claimChunks = [&]
    {
        for (;;)
        {
            const std::size_t chunk = nextChunk.fetch_add(1, std::memory_order_relaxed);
            if (chunk >= chunkCount)
            {
                return;
            }
            const std::size_t chunkBegin = begin + chunk * grain;
            function(chunkBegin, std::min(end, chunkBegin + grain));
        }
    };
    const std::size_t helpers = std::min<std::size_t>(chunkCount, jobs.ThreadCount()) - 1;
    JobCounter counter;
    for (std::size_t i = 0; i < helpers; ++i)
    {
        jobs.Submit([&claimChunks] { claimChunks(); }, &counter, priority);
    }
    claimChunks();
    jobs.WaitFor(counter);
}

/// Calls `function(i)` for every i in [begin, end), `grain` indices per job-sized chunk.
template <typename F>
void ParallelFor(JobSystem& jobs, std::size_t begin, std::size_t end, std::size_t grain, F&& function,
                 JobPriority priority = JobPriority::Normal)
{
    ParallelForChunks(
        jobs, begin, end, grain,
        [&function](std::size_t chunkBegin, std::size_t chunkEnd)
        {
            for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
            {
                function(i);
            }
        },
        priority);
}

/// Folds `combine(acc, map(i))` over [begin, end) starting each chunk from `identity`, then folds the chunk results
/// in order starting from `identity`. Returns `identity` for an empty range.
template <typename T, typename Map, typename Combine>
[[nodiscard]] T ParallelReduce(JobSystem& jobs, std::size_t begin, std::size_t end, std::size_t grain, T identity,
                               Map&& map, Combine&& combine, JobPriority priority = JobPriority::Normal)
{
    if (begin >= end)
    {
        return identity;
    }
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t count = end - begin;
    const std::size_t chunkCount = count / grain + (count % grain != 0 ? 1 : 0);
    std::vector<T> partials(chunkCount, identity);
    ParallelForChunks(
        jobs, begin, end, grain,
        [&](std::size_t chunkBegin, std::size_t chunkEnd)
        {
            T accumulator = identity;
            for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
            {
                accumulator = combine(std::move(accumulator), map(i));
            }
            partials[(chunkBegin - begin) / grain] = std::move(accumulator);
        },
        priority);
    T result = std::move(identity);
    for (T& partial : partials)
    {
        result = combine(std::move(result), std::move(partial));
    }
    return result;
}

enum class ScanMode : std::uint8_t
{
    Inclusive, ///< out[i] = in[0] + ... + in[i]
    Exclusive  ///< out[i] = in[0] + ... + in[i-1], out[0] = T{}
};

/// Prefix sum of `in` into `out` (same size; `out` may alias `in` exactly). T needs `T{}` as zero and `+`.
template <typename T>
void ParallelPrefixSum(JobSystem& jobs, std::span<const T> in, std::span<T> out, std::size_t grain,
                       ScanMode mode = ScanMode::Inclusive, JobPriority priority = JobPriority::Normal)
{
    INTERI_ASSERT(in.size() == out.size(), "ParallelPrefixSum: input and output sizes differ");
    const std::size_t count = in.size();
    if (count == 0)
    {
        return;
    }
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t chunkCount = count / grain + (count % grain != 0 ? 1 : 0);

    // Pass 1: each chunk's total. Then (serially) each chunk's starting offset. Pass 2: scan each chunk from there.
    std::vector<T> offsets(chunkCount, T{});
    ParallelForChunks(
        jobs, 0, count, grain,
        [&](std::size_t chunkBegin, std::size_t chunkEnd)
        {
            T sum{};
            for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
            {
                sum = sum + in[i];
            }
            offsets[chunkBegin / grain] = sum;
        },
        priority);
    T running{};
    for (T& offset : offsets)
    {
        const T chunkSum = offset;
        offset = running;
        running = running + chunkSum;
    }
    ParallelForChunks(
        jobs, 0, count, grain,
        [&](std::size_t chunkBegin, std::size_t chunkEnd)
        {
            T sum = offsets[chunkBegin / grain];
            for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
            {
                const T value = in[i]; // read before writing: out may alias in
                if (mode == ScanMode::Exclusive)
                {
                    out[i] = sum;
                    sum = sum + value;
                }
                else
                {
                    sum = sum + value;
                    out[i] = sum;
                }
            }
        },
        priority);
}

/// Elements per radix sort chunk (one 2 KiB histogram each).
inline constexpr std::size_t kRadixSortDefaultGrain = std::size_t{16} * 1024;

/// Stable LSD radix sort (8 bits per pass) of `data` by `key(element)`, an unsigned 32- or 64-bit integer. To sort
/// signed or floating-point keys, map them to unsigned keys with the same order. `scratch` must be as large as
/// `data`; its contents afterwards are unspecified. The result is in `data`.
template <typename T, typename KeyFn>
void ParallelRadixSort(JobSystem& jobs, std::span<T> data, std::span<T> scratch, KeyFn&& key,
                       std::size_t grain = kRadixSortDefaultGrain, JobPriority priority = JobPriority::Normal)
{
    using Key = std::invoke_result_t<KeyFn&, const T&>;
    static_assert(std::same_as<Key, std::uint32_t> || std::same_as<Key, std::uint64_t>,
                  "radix sort keys must be std::uint32_t or std::uint64_t");
    INTERI_ASSERT(scratch.size() >= data.size(), "ParallelRadixSort: scratch is smaller than data");
    const std::size_t count = data.size();
    if (count < 2)
    {
        return;
    }
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t chunkCount = count / grain + (count % grain != 0 ? 1 : 0);
    using Histogram = std::array<std::size_t, 256>;
    std::vector<Histogram> histograms(chunkCount);

    T* source = data.data();
    T* destination = scratch.data();
    for (unsigned shift = 0; shift < sizeof(Key) * 8; shift += 8)
    {
        const auto digit = [&key, shift](const T& element)
        { return static_cast<std::size_t>((static_cast<Key>(key(element)) >> shift) & 0xFF); };

        ParallelForChunks(
            jobs, 0, count, grain,
            [&](std::size_t chunkBegin, std::size_t chunkEnd)
            {
                Histogram& histogram = histograms[chunkBegin / grain];
                histogram.fill(0);
                for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
                {
                    ++histogram[digit(source[i])];
                }
            },
            priority);

        // Digit-major, chunk-minor offsets keep equal digits in their original order (stability).
        std::size_t running = 0;
        bool singleDigit = false;
        for (std::size_t d = 0; d < 256; ++d)
        {
            const std::size_t digitStart = running;
            for (Histogram& histogram : histograms)
            {
                const std::size_t bucket = histogram[d];
                histogram[d] = running;
                running += bucket;
            }
            singleDigit = singleDigit || (running - digitStart == count);
        }
        if (singleDigit)
        {
            continue; // every element has the same digit: this pass would not move anything
        }

        ParallelForChunks(
            jobs, 0, count, grain,
            [&](std::size_t chunkBegin, std::size_t chunkEnd)
            {
                Histogram& offsets = histograms[chunkBegin / grain];
                for (std::size_t i = chunkBegin; i < chunkEnd; ++i)
                {
                    destination[offsets[digit(source[i])]++] = std::move(source[i]);
                }
            },
            priority);
        std::swap(source, destination);
    }
    if (source != data.data())
    {
        ParallelForChunks(
            jobs, 0, count, grain, [&](std::size_t chunkBegin, std::size_t chunkEnd)
            { std::move(source + chunkBegin, source + chunkEnd, data.data() + chunkBegin); }, priority);
    }
}

/// Radix sort of unsigned 32/64-bit integers by value.
template <typename T>
    requires std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>
void ParallelRadixSort(JobSystem& jobs, std::span<T> data, std::span<T> scratch,
                       std::size_t grain = kRadixSortDefaultGrain, JobPriority priority = JobPriority::Normal)
{
    ParallelRadixSort(jobs, data, scratch, [](const T& value) { return value; }, grain, priority);
}

} // namespace interi

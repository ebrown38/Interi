// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <numeric>
#include <span>
#include <vector>

using interi::JobSystem;
using interi::JobSystemDesc;
using interi::ScanMode;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// Threads (workers + caller) used by the "identical at 1/4/32 threads" checks.
constexpr std::uint32_t kWorkerCounts[] = {0, 3, 31};

std::vector<float> RandomFloats(std::uint64_t seed, std::size_t count)
{
    interi::testing::DeterministicRng rng(seed);
    std::vector<float> values(count);
    for (float& value : values)
    {
        value = rng.NextFloat(-1000.0f, 1000.0f);
    }
    return values;
}

class ParallelAlgorithms : public interi::testing::SeededTest
{
};

} // namespace

TEST(ParallelFor, EmptyRange_RunsNoJobs)
{
    JobSystem jobs(Workers(2));
    int calls = 0;
    interi::ParallelFor(jobs, 5, 5, 1, [&calls](std::size_t) { ++calls; });
    interi::ParallelFor(jobs, 7, 3, 1, [&calls](std::size_t) { ++calls; });
    EXPECT_EQ(calls, 0);
}

class ParallelForGrain : public ::testing::TestWithParam<std::size_t>
{
};

TEST_P(ParallelForGrain, EachIndexVisitedOnce)
{
    JobSystem jobs(Workers(4));
    constexpr std::size_t kBegin = 3;
    constexpr std::size_t kEnd = 10'007;
    auto visits = std::make_unique<std::atomic<int>[]>(kEnd);
    interi::ParallelFor(jobs, kBegin, kEnd, GetParam(),
                        [&visits](std::size_t i) { visits[i].fetch_add(1, std::memory_order_relaxed); });
    int wrong = 0;
    for (std::size_t i = 0; i < kEnd; ++i)
    {
        wrong += visits[i].load() == (i >= kBegin ? 1 : 0) ? 0 : 1;
    }
    EXPECT_EQ(wrong, 0);
}

INSTANTIATE_TEST_SUITE_P(Grains, ParallelForGrain, ::testing::Values(0, 1, 7, 64, 1000, 10'004, 1'000'000));

TEST(ParallelFor, Chunks_CoverRangeWithGrainSizedPieces)
{
    JobSystem jobs(Workers(4));
    std::atomic<std::size_t> covered{0};
    std::atomic<int> oversized{0};
    interi::ParallelForChunks(jobs, 0, 1000, 64,
                              [&](std::size_t begin, std::size_t end)
                              {
                                  covered.fetch_add(end - begin);
                                  if (end - begin > 64 || (begin % 64) != 0)
                                  {
                                      oversized.fetch_add(1);
                                  }
                              });
    EXPECT_EQ(covered.load(), 1000u);
    EXPECT_EQ(oversized.load(), 0);
}

TEST_F(ParallelAlgorithms, ParallelReduce_MatchesSerial)
{
    JobSystem jobs(Workers(4));
    std::vector<std::int64_t> values(100'003);
    for (std::int64_t& value : values)
    {
        value = Rng().NextInt(-1'000'000, 1'000'000);
    }
    const std::int64_t serial = std::accumulate(values.begin(), values.end(), std::int64_t{0});
    const std::int64_t parallel = interi::ParallelReduce(
        jobs, 0, values.size(), 1000, std::int64_t{0}, [&values](std::size_t i) { return values[i]; },
        [](std::int64_t a, std::int64_t b) { return a + b; });
    EXPECT_EQ(parallel, serial);

    const std::int64_t empty = interi::ParallelReduce(
        jobs, 4, 4, 1000, std::int64_t{42}, [](std::size_t) { return std::int64_t{1}; },
        [](std::int64_t a, std::int64_t b) { return a + b; });
    EXPECT_EQ(empty, 42);
}

TEST_F(ParallelAlgorithms, ParallelReduce_FloatSum_IdenticalAcrossThreadCounts)
{
    const std::vector<float> values = RandomFloats(Seed(), 250'000);
    std::vector<std::uint32_t> bits;
    for (const std::uint32_t workers : kWorkerCounts)
    {
        JobSystem jobs(Workers(workers));
        const float sum = interi::ParallelReduce(
            jobs, 0, values.size(), 1024, 0.0f, [&values](std::size_t i) { return values[i]; },
            [](float a, float b) { return a + b; });
        bits.push_back(std::bit_cast<std::uint32_t>(sum));
    }
    EXPECT_EQ(bits[0], bits[1]);
    EXPECT_EQ(bits[0], bits[2]);
}

TEST_F(ParallelAlgorithms, ParallelSort_MatchesStdSort)
{
    JobSystem jobs(Workers(4));
    for (const std::size_t count :
         {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{4097}, std::size_t{200'000}})
    {
        std::vector<std::uint32_t> keys(count);
        for (std::uint32_t& key : keys)
        {
            key = static_cast<std::uint32_t>(Rng().NextU64());
        }
        std::vector<std::uint32_t> expected = keys;
        std::sort(expected.begin(), expected.end());
        std::vector<std::uint32_t> scratch(count);
        interi::ParallelRadixSort(jobs, std::span<std::uint32_t>(keys), std::span<std::uint32_t>(scratch));
        EXPECT_EQ(keys, expected) << count << " keys";
    }
}

TEST_F(ParallelAlgorithms, ParallelSort_KeyFunction_IsStable)
{
    struct Item
    {
        std::uint64_t key;
        std::uint32_t originalIndex;
        bool operator==(const Item&) const = default;
    };
    JobSystem jobs(Workers(4));
    std::vector<Item> items(100'000);
    for (std::uint32_t i = 0; i < items.size(); ++i)
    {
        // Few distinct keys, spread over high bits so several passes run and stability matters.
        items[i] = {static_cast<std::uint64_t>(Rng().NextInt(0, 63)) << 40, i};
    }
    std::vector<Item> expected = items;
    std::stable_sort(expected.begin(), expected.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
    std::vector<Item> scratch(items.size());
    interi::ParallelRadixSort(jobs, std::span<Item>(items), std::span<Item>(scratch),
                              [](const Item& item) { return item.key; });
    EXPECT_TRUE(items == expected);
}

TEST_F(ParallelAlgorithms, ParallelSort_IdenticalAcrossThreadCounts)
{
    std::vector<std::uint64_t> input(150'000);
    for (std::uint64_t& key : input)
    {
        key = Rng().NextU64();
    }
    std::vector<std::vector<std::uint64_t>> results;
    for (const std::uint32_t workers : kWorkerCounts)
    {
        JobSystem jobs(Workers(workers));
        std::vector<std::uint64_t> keys = input;
        std::vector<std::uint64_t> scratch(keys.size());
        interi::ParallelRadixSort(jobs, std::span<std::uint64_t>(keys), std::span<std::uint64_t>(scratch));
        results.push_back(std::move(keys));
    }
    EXPECT_EQ(results[0], results[1]);
    EXPECT_EQ(results[0], results[2]);
    EXPECT_TRUE(std::is_sorted(results[0].begin(), results[0].end()));
}

TEST_F(ParallelAlgorithms, ParallelPrefixSum_MatchesStdScan)
{
    JobSystem jobs(Workers(4));
    for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{999}, std::size_t{100'001}})
    {
        std::vector<std::int64_t> values(count);
        for (std::int64_t& value : values)
        {
            value = Rng().NextInt(-100, 100);
        }
        std::vector<std::int64_t> inclusive(count);
        std::vector<std::int64_t> exclusive(count);
        interi::ParallelPrefixSum(jobs, std::span<const std::int64_t>(values), std::span<std::int64_t>(inclusive), 256,
                                  ScanMode::Inclusive);
        interi::ParallelPrefixSum(jobs, std::span<const std::int64_t>(values), std::span<std::int64_t>(exclusive), 256,
                                  ScanMode::Exclusive);

        std::vector<std::int64_t> expectedInclusive(count);
        std::vector<std::int64_t> expectedExclusive(count);
        std::inclusive_scan(values.begin(), values.end(), expectedInclusive.begin());
        std::exclusive_scan(values.begin(), values.end(), expectedExclusive.begin(), std::int64_t{0});
        EXPECT_EQ(inclusive, expectedInclusive) << count;
        EXPECT_EQ(exclusive, expectedExclusive) << count;
    }
}

TEST_F(ParallelAlgorithms, ParallelPrefixSum_InPlace)
{
    JobSystem jobs(Workers(3));
    std::vector<std::int64_t> values(5000);
    for (std::int64_t& value : values)
    {
        value = Rng().NextInt(0, 9);
    }
    std::vector<std::int64_t> expected(values.size());
    std::exclusive_scan(values.begin(), values.end(), expected.begin(), std::int64_t{0});
    interi::ParallelPrefixSum(jobs, std::span<const std::int64_t>(values), std::span<std::int64_t>(values), 100,
                              ScanMode::Exclusive);
    EXPECT_EQ(values, expected);
}

TEST_F(ParallelAlgorithms, ParallelPrefixSum_FloatIdenticalAcrossThreadCounts)
{
    const std::vector<float> values = RandomFloats(Seed(), 120'000);
    std::vector<std::vector<float>> results;
    for (const std::uint32_t workers : kWorkerCounts)
    {
        JobSystem jobs(Workers(workers));
        std::vector<float> out(values.size());
        interi::ParallelPrefixSum(jobs, std::span<const float>(values), std::span<float>(out), 512,
                                  ScanMode::Inclusive);
        results.push_back(std::move(out));
    }
    // Compare bit patterns: float == would also accept -0 == +0.
    const auto bits = [](const std::vector<float>& v)
    {
        std::vector<std::uint32_t> out(v.size());
        std::transform(v.begin(), v.end(), out.begin(), [](float f) { return std::bit_cast<std::uint32_t>(f); });
        return out;
    };
    EXPECT_EQ(bits(results[0]), bits(results[1]));
    EXPECT_EQ(bits(results[0]), bits(results[2]));
}

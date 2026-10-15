// SPDX-License-Identifier: MIT
#include <interi/core/Random.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

TEST(Random, SeedZero_MatchesSplitMix64Reference)
{
    // SplitMix64 reference outputs for seed 0 (identical on every platform).
    interi::Random random(0);
    EXPECT_EQ(random.NextU64(), 0xE220A8397B1DCDAFull);
    EXPECT_EQ(random.NextU64(), 0x6E789E6AA1B965F4ull);
    EXPECT_EQ(random.NextU64(), 0x06C45D188009454Full);
}

TEST(Random, SameSeed_SameSequence)
{
    interi::Random a(1234);
    interi::Random b(1234);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(a.NextFloat(-2.0f, 3.0f), b.NextFloat(-2.0f, 3.0f));
    }
}

TEST(Random, NextFloat_StaysInRange)
{
    interi::Random random(7);
    for (int i = 0; i < 10000; ++i)
    {
        const float value = random.NextFloat(-1.5f, 2.5f);
        EXPECT_GE(value, -1.5f);
        EXPECT_LT(value, 2.5f);
    }
}

TEST(Random, NextBelow_CoversRangeWithoutOverflow)
{
    interi::Random random(99);
    std::array<int, 5> counts{};
    for (int i = 0; i < 5000; ++i)
    {
        const std::uint32_t value = random.NextBelow(5);
        ASSERT_LT(value, 5u);
        ++counts[value];
    }
    for (const int count : counts)
    {
        EXPECT_GT(count, 800);
    }
}

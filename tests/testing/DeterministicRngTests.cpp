// SPDX-License-Identifier: MIT
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

using interi::testing::DeterministicRng;
using interi::testing::kDefaultTestSeed;
using interi::testing::ResolveTestSeed;

TEST(DeterministicRng, KnownSeed_MatchesSplitMix64ReferenceValue)
{
    // Reference output of SplitMix64 for seed 0. Guarantees identical sequences on every
    // compiler/standard library (std:: distributions are not portable).
    DeterministicRng rng(0);
    EXPECT_EQ(rng.NextU64(), 0xE220A8397B1DCDAFull);
}

TEST(DeterministicRng, SameSeed_SameSequence)
{
    DeterministicRng a(1234);
    DeterministicRng b(1234);
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_EQ(a.NextU64(), b.NextU64()) << "diverged at index " << i;
    }
}

TEST(DeterministicRng, DifferentSeeds_DifferentSequences)
{
    DeterministicRng a(1);
    DeterministicRng b(2);
    EXPECT_NE(a.NextU64(), b.NextU64());
}

TEST(DeterministicRng, NextFloat_StaysWithinHalfOpenRange)
{
    DeterministicRng rng(42);
    for (int i = 0; i < 10000; ++i)
    {
        const float value = rng.NextFloat(-2.0f, 3.0f);
        ASSERT_GE(value, -2.0f);
        ASSERT_LT(value, 3.0f);
    }
}

TEST(DeterministicRng, NextInt_InclusiveBoundsBothReached)
{
    DeterministicRng rng(7);
    bool sawMin = false;
    bool sawMax = false;
    for (int i = 0; i < 10000; ++i)
    {
        const std::int64_t value = rng.NextInt(-3, 3);
        ASSERT_GE(value, -3);
        ASSERT_LE(value, 3);
        sawMin |= value == -3;
        sawMax |= value == 3;
    }
    EXPECT_TRUE(sawMin);
    EXPECT_TRUE(sawMax);
}

TEST(ResolveTestSeed, NullOrEmpty_ReturnsDefault)
{
    EXPECT_EQ(ResolveTestSeed(nullptr), kDefaultTestSeed);
    EXPECT_EQ(ResolveTestSeed(""), kDefaultTestSeed);
}

TEST(ResolveTestSeed, DecimalAndHex_Parsed)
{
    EXPECT_EQ(ResolveTestSeed("12345"), 12345u);
    EXPECT_EQ(ResolveTestSeed("0xFF"), 255u);
}

TEST(ResolveTestSeed, Garbage_Throws)
{
    EXPECT_THROW((void)ResolveTestSeed("not-a-seed"), std::invalid_argument);
    EXPECT_THROW((void)ResolveTestSeed("12abc"), std::invalid_argument);
}

class SeededTestFixtureTests : public interi::testing::SeededTest
{
};

TEST_F(SeededTestFixtureTests, Rng_SeededFromResolvedSeed)
{
    DeterministicRng expected(Seed());
    EXPECT_EQ(Rng().NextU64(), expected.NextU64());
}

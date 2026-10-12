// SPDX-License-Identifier: MIT
#include <interi/testing/StateHasher.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using interi::testing::StateHasher;

namespace
{
struct PackedState
{
    std::uint32_t id;
    std::uint32_t flags;
};

struct PaddedState
{
    std::uint8_t tag;
    std::uint64_t value; // 7 padding bytes before this: indeterminate, must not be hashed
};
} // namespace

static_assert(interi::testing::Hashable<PackedState>);
static_assert(!interi::testing::Hashable<PaddedState>, "types with padding must be rejected at compile time");

TEST(StateHasher, EmptyInput_MatchesXXH3ReferenceValue)
{
    // XXH3_64bits("") with seed 0. Pins the algorithm so hashes are comparable across platforms.
    EXPECT_EQ(StateHasher{}.Finish(), 0x2D06800538D394C2ull);
}

TEST(StateHasher, SameInput_SameHash)
{
    StateHasher a;
    StateHasher b;
    for (StateHasher* hasher : {&a, &b})
    {
        hasher->Add(PackedState{7, 3});
        hasher->Add(1.5f);
        hasher->Add(2.25);
    }
    EXPECT_EQ(a.Finish(), b.Finish());
}

TEST(StateHasher, FloatBitDifference_DifferentHash)
{
    StateHasher a;
    StateHasher b;
    a.Add(1.0f);
    b.Add(std::nextafter(1.0f, 2.0f)); // one ULP apart
    EXPECT_NE(a.Finish(), b.Finish());
}

TEST(StateHasher, NegativeZero_DiffersFromPositiveZero)
{
    // Hashing is by bit pattern; determinism checks must catch sign differences too.
    StateHasher a;
    StateHasher b;
    a.Add(0.0f);
    b.Add(-0.0f);
    EXPECT_NE(a.Finish(), b.Finish());
}

TEST(StateHasher, OrderMatters)
{
    StateHasher a;
    StateHasher b;
    a.Add(1u);
    a.Add(2u);
    b.Add(2u);
    b.Add(1u);
    EXPECT_NE(a.Finish(), b.Finish());
}

TEST(StateHasher, SpanOfValues_EqualsElementwiseAdds)
{
    const std::vector<float> values{1.0f, -2.0f, 3.5f};
    StateHasher bulk;
    bulk.AddRange(std::span<const float>(values));
    StateHasher each;
    for (float v : values)
    {
        each.Add(v);
    }
    EXPECT_EQ(bulk.Finish(), each.Finish());
}

TEST(StateHasher, Finish_DoesNotConsumeState)
{
    StateHasher hasher;
    hasher.Add(42u);
    const std::uint64_t first = hasher.Finish();
    EXPECT_EQ(hasher.Finish(), first);
    hasher.Add(43u);
    EXPECT_NE(hasher.Finish(), first);
}

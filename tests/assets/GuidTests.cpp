// SPDX-License-Identifier: MIT
#include <interi/assets/Guid.h>

#include <gtest/gtest.h>

#include <set>

using namespace interi::assets;

TEST(Guid, ToStringThenParse_RoundTrips)
{
    const Guid guid{.high = 0x0123456789abcdefULL, .low = 0xfedcba9876543210ULL};
    EXPECT_EQ(ToString(guid), "0123456789abcdeffedcba9876543210");
    EXPECT_EQ(ParseGuid(ToString(guid)), guid);
}

TEST(Guid, ParseUpperCase_Accepted)
{
    EXPECT_EQ(ParseGuid("0123456789ABCDEFFEDCBA9876543210"), (Guid{0x0123456789abcdefULL, 0xfedcba9876543210ULL}));
}

TEST(Guid, ParseBadText_Rejected)
{
    EXPECT_FALSE(ParseGuid("").has_value());
    EXPECT_FALSE(ParseGuid("0123456789abcdef").has_value());
    EXPECT_FALSE(ParseGuid("0123456789abcdeffedcba987654321g").has_value());
    EXPECT_FALSE(ParseGuid("0123456789abcdeffedcba98765432100").has_value());
}

TEST(Guid, NullGuid_IsNullAndPrintsZeros)
{
    EXPECT_TRUE(Guid{}.IsNull());
    EXPECT_EQ(ToString(Guid{}), std::string(32, '0'));
}

TEST(Guid, NewGuid_IsNeverNullAndDiffers)
{
    std::set<Guid> seen;
    for (int i = 0; i < 64; ++i)
    {
        const Guid guid = NewGuid();
        EXPECT_FALSE(guid.IsNull());
        seen.insert(guid);
    }
    EXPECT_EQ(seen.size(), 64u);
}

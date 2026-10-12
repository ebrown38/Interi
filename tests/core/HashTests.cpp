// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Hash.h>
#include <interi/core/StringId.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

TEST(Hash, Hash64_SameInputSameHash)
{
    EXPECT_EQ(interi::Hash64("concrete_wall"), interi::Hash64(std::string("concrete_wall")));
    EXPECT_NE(interi::Hash64("concrete_wall"), interi::Hash64("concrete_walk"));
}

TEST(Hash, Hash64_EmptyInputMatchesXXH3Reference)
{
    // XXH3_64bits("", 0) reference value from the xxHash test vectors.
    EXPECT_EQ(interi::Hash64(std::string_view{}), 0x2D06800538D394C2ull);
}

TEST(Hash, Seed_ChangesHash)
{
    EXPECT_NE(interi::Hash64("steel", 0), interi::Hash64("steel", 1));
}

TEST(Hash, HashCombine_OrderMatters)
{
    const std::uint64_t a = interi::Hash64("a");
    const std::uint64_t b = interi::Hash64("b");
    EXPECT_NE(interi::HashCombine(a, b), interi::HashCombine(b, a));
}

TEST(Hash, IntegerHasher_SpreadsSequentialKeys)
{
    // Open-addressing tables index with the low bits, so sequential keys must not collide there.
    const interi::Hasher<std::uint32_t> hasher;
    std::unordered_set<std::uint64_t> lowBits;
    for (std::uint32_t key = 0; key < 1024; ++key)
    {
        lowBits.insert(hasher(key) & 0xFFFF);
    }
    EXPECT_GT(lowBits.size(), 1000u);
}

TEST(Hash, StringHasher_AcceptsStringAndViewAlike)
{
    const interi::Hasher<std::string> hasher;
    EXPECT_EQ(hasher(std::string("rebar")), interi::Hasher<std::string_view>{}("rebar"));
}

// --- StringId --------------------------------------------------------------------------------------------------------

TEST(StringId, SameName_EqualIds)
{
    EXPECT_EQ(interi::StringId("Destruction.Chunk"), interi::StringId(std::string("Destruction.Chunk")));
    EXPECT_NE(interi::StringId("Destruction.Chunk"), interi::StringId("Destruction.Bond"));
}

TEST(StringId, Value_IsHash64OfName)
{
    EXPECT_EQ(interi::StringId("material.concrete").Value(), interi::Hash64("material.concrete"));
}

TEST(StringId, Empty_IsInvalidAndEqualsDefault)
{
    EXPECT_FALSE(interi::StringId().IsValid());
    EXPECT_EQ(interi::StringId(""), interi::StringId());
    EXPECT_TRUE(interi::StringId("x").IsValid());
}

TEST(StringId, DebugName_ReturnsOriginalWhenNamesEnabled)
{
    const interi::StringId id("wall_section_04");
    if constexpr (interi::kStringIdNamesEnabled)
    {
        EXPECT_EQ(id.DebugName(), "wall_section_04");
    }
    else
    {
        EXPECT_EQ(id.DebugName(), "");
    }
}

TEST(StringId, UsableAsUnorderedMapKey)
{
    std::unordered_map<interi::StringId, int> materials;
    materials[interi::StringId("steel")] = 1;
    materials[interi::StringId("glass")] = 2;
    EXPECT_EQ(materials.at(interi::StringId("glass")), 2);
}

namespace
{
int g_collisionAsserts = 0;
} // namespace

TEST(StringId, Collision_DetectedInDebug)
{
    if constexpr (!interi::kStringIdNamesEnabled || !interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "the StringId name table is compiled out in this configuration";
    }
    g_collisionAsserts = 0;
    const interi::ScopedAssertHandler handler(
        [](const interi::AssertInfo&)
        {
            ++g_collisionAsserts;
            return interi::AssertAction::Continue;
        });

    // A real 64-bit XXH3 collision cannot be found on demand, so force two names onto the same hash.
    constexpr std::uint64_t kForcedHash = 0xDEAD'BEEF'0000'0001ull;
    const interi::StringId first = interi::detail::RegisterStringId("collision_a", kForcedHash);
    EXPECT_EQ(g_collisionAsserts, 0);
    EXPECT_EQ(interi::detail::RegisterStringId("collision_a", kForcedHash), first) << "same name again is fine";
    EXPECT_EQ(g_collisionAsserts, 0);
    interi::detail::RegisterStringId("collision_b", kForcedHash);
    EXPECT_EQ(g_collisionAsserts, 1);
}

// SPDX-License-Identifier: MIT
#include <interi/core/StringId.h>
#include <interi/core/containers/HashMap.h>
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <unordered_map>

TEST(HashMap, InsertFind_Basic)
{
    interi::HashMap<int, std::string> map;
    EXPECT_TRUE(map.Insert(1, "one").second);
    EXPECT_TRUE(map.Insert(2, "two").second);
    EXPECT_FALSE(map.Insert(1, "uno").second) << "existing key is not overwritten by Insert";
    ASSERT_NE(map.Find(1), nullptr);
    EXPECT_EQ(*map.Find(1), "one");
    EXPECT_EQ(map.Find(3), nullptr);
    EXPECT_EQ(map.Size(), 2u);
}

TEST(HashMap, SubscriptOperator_DefaultConstructsMissing)
{
    interi::HashMap<std::string, int> counts;
    ++counts["wall"];
    ++counts["wall"];
    ++counts["floor"];
    EXPECT_EQ(counts["wall"], 2);
    EXPECT_EQ(counts["floor"], 1);
}

TEST(HashMap, Rehash_PreservesEntries)
{
    interi::HashMap<std::uint32_t, std::uint32_t> map;
    const std::size_t initialCapacity = map.Capacity();
    for (std::uint32_t i = 0; i < 10'000; ++i)
    {
        map.Insert(i, i * 3);
    }
    EXPECT_GT(map.Capacity(), initialCapacity);
    ASSERT_EQ(map.Size(), 10'000u);
    for (std::uint32_t i = 0; i < 10'000; ++i)
    {
        const std::uint32_t* value = map.Find(i);
        ASSERT_NE(value, nullptr) << i;
        EXPECT_EQ(*value, i * 3);
    }
    EXPECT_LE(map.LoadFactor(), (interi::HashMap<std::uint32_t, std::uint32_t>::kMaxLoadFactor));
}

TEST(HashMap, Erase_KeepsOtherKeysReachable)
{
    interi::HashMap<int, int> map;
    for (int i = 0; i < 200; ++i)
    {
        map.Insert(i, i);
    }
    for (int i = 0; i < 200; i += 2)
    {
        EXPECT_TRUE(map.Erase(i));
    }
    EXPECT_FALSE(map.Erase(0));
    EXPECT_EQ(map.Size(), 100u);
    for (int i = 0; i < 200; ++i)
    {
        EXPECT_EQ(map.Contains(i), i % 2 == 1) << i;
    }
}

TEST(HashMap, TransparentLookup_FindsStringByView)
{
    interi::HashMap<std::string, int> map;
    map.Insert("concrete", 1);
    const std::string_view key = "concrete";
    ASSERT_NE(map.Find(key), nullptr);
    EXPECT_EQ(*map.Find(key), 1);
}

TEST(HashMap, Iteration_VisitsEachEntryOnce)
{
    interi::HashMap<interi::StringId, int> map;
    map.Insert(interi::StringId("a"), 1);
    map.Insert(interi::StringId("b"), 2);
    map.Insert(interi::StringId("c"), 3);
    int sum = 0;
    int count = 0;
    for (const auto& [key, value] : map)
    {
        EXPECT_TRUE(key.IsValid());
        sum += value;
        ++count;
    }
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 6);
}

TEST(HashMap, CopyAndMove_PreserveContents)
{
    interi::HashMap<std::string, std::string> map;
    map.Insert("k", "v");
    interi::HashMap<std::string, std::string> copy = map;
    interi::HashMap<std::string, std::string> moved = std::move(map);
    EXPECT_EQ(*copy.Find("k"), "v");
    EXPECT_EQ(*moved.Find("k"), "v");
    EXPECT_EQ(map.Size(), 0u); // NOLINT(bugprone-use-after-move): moved-from map is specified as empty
}

TEST(HashMap, Clear_RemovesEverythingAndStaysUsable)
{
    interi::HashMap<int, std::string> map;
    for (int i = 0; i < 50; ++i)
    {
        map.Insert(i, std::to_string(i));
    }
    map.Clear();
    EXPECT_EQ(map.Size(), 0u);
    EXPECT_EQ(map.Find(5), nullptr);
    map.Insert(5, "five");
    EXPECT_EQ(*map.Find(5), "five");
}

class HashMapRandomized : public interi::testing::SeededTest
{
};

TEST_F(HashMapRandomized, RandomOps_MatchStdUnorderedMap)
{
    interi::HashMap<std::uint64_t, std::uint64_t> map;
    std::unordered_map<std::uint64_t, std::uint64_t> reference;
    for (int op = 0; op < 20'000; ++op)
    {
        const auto key = static_cast<std::uint64_t>(Rng().NextInt(0, 2000));
        switch (Rng().NextInt(0, 2))
        {
        case 0:
            EXPECT_EQ(map.Insert(key, key + 1).second, reference.emplace(key, key + 1).second);
            break;
        case 1:
            EXPECT_EQ(map.Erase(key), reference.erase(key) == 1);
            break;
        default:
            EXPECT_EQ(map.Contains(key), reference.contains(key));
            break;
        }
    }
    ASSERT_EQ(map.Size(), reference.size());
    for (const auto& [key, value] : reference)
    {
        ASSERT_NE(map.Find(key), nullptr);
        EXPECT_EQ(*map.Find(key), value);
    }
}

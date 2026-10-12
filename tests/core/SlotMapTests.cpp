// SPDX-License-Identifier: MIT
#include <interi/core/containers/SlotMap.h>
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Chunk
{
    int id = 0;
};
} // namespace

TEST(Handle, Default_IsInvalid)
{
    const interi::Handle<Chunk> handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle, interi::Handle<Chunk>{});
}

TEST(SlotMap, Insert_ReturnsHandleThatFindsValue)
{
    interi::SlotMap<Chunk> chunks;
    const auto a = chunks.Insert(Chunk{1});
    const auto b = chunks.Emplace(2);
    ASSERT_TRUE(a.IsValid());
    EXPECT_NE(a, b);
    ASSERT_NE(chunks.Get(a), nullptr);
    EXPECT_EQ(chunks.Get(a)->id, 1);
    EXPECT_EQ(chunks.Get(b)->id, 2);
    EXPECT_EQ(chunks.Size(), 2u);
}

TEST(SlotMap, StaleHandle_LookupFails)
{
    interi::SlotMap<Chunk> chunks;
    const auto handle = chunks.Insert(Chunk{7});
    EXPECT_TRUE(chunks.Remove(handle));
    EXPECT_EQ(chunks.Get(handle), nullptr);
    EXPECT_FALSE(chunks.Contains(handle));
    EXPECT_FALSE(chunks.Remove(handle)) << "double remove is rejected";

    // The slot is reused, but the old handle must not see the new occupant.
    const auto reused = chunks.Insert(Chunk{8});
    EXPECT_EQ(reused.Index(), handle.Index());
    EXPECT_NE(reused.Generation(), handle.Generation());
    EXPECT_EQ(chunks.Get(handle), nullptr);
    EXPECT_EQ(chunks.Get(reused)->id, 8);
}

TEST(SlotMap, GenerationWrap_HandledSafely)
{
    // MaxGeneration = 3 makes wrapping reachable; production maps use the full 32-bit range.
    interi::SlotMap<Chunk, 3> chunks;
    std::vector<interi::Handle<Chunk>> retired;
    for (int i = 0; i < 3; ++i)
    {
        const auto handle = chunks.Insert(Chunk{i});
        EXPECT_EQ(handle.Index(), 0u);
        EXPECT_EQ(handle.Generation(), static_cast<std::uint32_t>(i + 1));
        chunks.Remove(handle);
        retired.push_back(handle);
    }
    // Slot 0 reached its last generation: it is retired instead of wrapping back to generation 1.
    const auto fresh = chunks.Insert(Chunk{99});
    EXPECT_EQ(fresh.Index(), 1u);
    for (const auto& old : retired)
    {
        EXPECT_EQ(chunks.Get(old), nullptr);
    }
    EXPECT_EQ(chunks.RetiredSlotCount(), 1u);
}

TEST(SlotMap, ForeignHandle_OutOfRange_LookupFails)
{
    interi::SlotMap<Chunk> chunks;
    const auto handle = interi::Handle<Chunk>::FromParts(1000, 1);
    EXPECT_EQ(chunks.Get(handle), nullptr);
    EXPECT_FALSE(chunks.Remove(handle));
}

TEST(SlotMap, Iteration_VisitsLiveValuesDensely)
{
    interi::SlotMap<Chunk> chunks;
    std::vector<interi::Handle<Chunk>> handles;
    for (int i = 0; i < 10; ++i)
    {
        handles.push_back(chunks.Insert(Chunk{i}));
    }
    chunks.Remove(handles[3]);
    chunks.Remove(handles[0]);

    std::vector<int> seen;
    for (const Chunk& chunk : chunks)
    {
        seen.push_back(chunk.id);
    }
    std::ranges::sort(seen);
    EXPECT_EQ(seen, (std::vector<int>{1, 2, 4, 5, 6, 7, 8, 9}));

    // Swap-removal must keep every remaining handle pointing at the right value.
    for (int i = 1; i < 10; ++i)
    {
        if (i != 3)
        {
            ASSERT_NE(chunks.Get(handles[static_cast<std::size_t>(i)]), nullptr);
            EXPECT_EQ(chunks.Get(handles[static_cast<std::size_t>(i)])->id, i);
        }
    }
}

TEST(SlotMap, HandleOfDenseIndex_RoundTrips)
{
    interi::SlotMap<Chunk> chunks;
    const auto a = chunks.Insert(Chunk{1});
    const auto b = chunks.Insert(Chunk{2});
    chunks.Remove(a);
    ASSERT_EQ(chunks.Size(), 1u);
    EXPECT_EQ(chunks.HandleAt(0), b);
}

TEST(SlotMap, Clear_InvalidatesAllHandles)
{
    interi::SlotMap<std::unique_ptr<int>> owners;
    const auto a = owners.Insert(std::make_unique<int>(1));
    owners.Clear();
    EXPECT_EQ(owners.Size(), 0u);
    EXPECT_EQ(owners.Get(a), nullptr);
    const auto b = owners.Insert(std::make_unique<int>(2));
    EXPECT_NE(a, b);
}

class SlotMapRandomized : public interi::testing::SeededTest
{
};

TEST_F(SlotMapRandomized, RandomOps_MatchReferenceModel)
{
    interi::SlotMap<std::string> map;
    std::map<std::uint64_t, std::pair<interi::Handle<std::string>, std::string>> model;
    std::vector<interi::Handle<std::string>> removed;
    std::uint64_t nextKey = 0;
    for (int op = 0; op < 5000; ++op)
    {
        if (model.empty() || Rng().NextInt(0, 99) < 60)
        {
            std::string value = "v" + std::to_string(op);
            model[nextKey++] = {map.Insert(value), value};
        }
        else
        {
            auto it = model.begin();
            std::advance(it, Rng().NextInt(0, static_cast<std::int64_t>(model.size()) - 1));
            ASSERT_TRUE(map.Remove(it->second.first));
            removed.push_back(it->second.first);
            model.erase(it);
        }
    }
    ASSERT_EQ(map.Size(), model.size());
    for (const auto& [key, entry] : model)
    {
        ASSERT_NE(map.Get(entry.first), nullptr);
        EXPECT_EQ(*map.Get(entry.first), entry.second);
    }
    for (const auto& handle : removed)
    {
        EXPECT_EQ(map.Get(handle), nullptr);
    }
}

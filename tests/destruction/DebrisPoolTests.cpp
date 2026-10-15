// SPDX-License-Identifier: MIT
#include <interi/core/memory/MemoryTracking.h>
#include <interi/destruction/DebrisPool.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

using namespace interi::destruction;

TEST(DebrisPool, Empty_HasCapacityAndNoLiveSlots)
{
    DebrisPool pool(4);
    EXPECT_EQ(pool.Capacity(), 4u);
    EXPECT_EQ(pool.LiveCount(), 0u);
    EXPECT_FALSE(pool.IsFull());
}

TEST(DebrisPool, Acquire_BelowCap_EvictsNothing)
{
    DebrisPool pool(2);
    const DebrisPool::Acquired a = pool.Acquire(10, 1.0f);
    const DebrisPool::Acquired b = pool.Acquire(11, 2.0f);
    ASSERT_TRUE(a.slot.IsValid());
    ASSERT_TRUE(b.slot.IsValid());
    EXPECT_FALSE(a.evicted.has_value());
    EXPECT_FALSE(b.evicted.has_value());
    EXPECT_NE(a.slot.index, b.slot.index);
    EXPECT_EQ(pool.User(a.slot), 10u);
    EXPECT_EQ(pool.User(b.slot), 11u);
    EXPECT_TRUE(pool.IsFull());
}

TEST(DebrisPool, AtCap_EvictsLowestPriority)
{
    DebrisPool pool(3);
    const DebrisSlot a = pool.Acquire(100, 5.0f).slot;
    const DebrisSlot b = pool.Acquire(101, 1.0f).slot;
    const DebrisSlot c = pool.Acquire(102, 3.0f).slot;

    const DebrisPool::Acquired d = pool.Acquire(103, 4.0f);
    ASSERT_TRUE(d.slot.IsValid());
    ASSERT_TRUE(d.evicted.has_value());
    EXPECT_EQ(*d.evicted, 101u);
    EXPECT_FALSE(pool.IsLive(b));
    EXPECT_TRUE(pool.IsLive(a));
    EXPECT_TRUE(pool.IsLive(c));
    EXPECT_EQ(pool.User(d.slot), 103u);
    EXPECT_EQ(pool.LiveCount(), 3u);
}

TEST(DebrisPool, AtCap_UpdatedPriorityDecidesEviction)
{
    DebrisPool pool(2);
    const DebrisSlot a = pool.Acquire(1, 1.0f).slot;
    const DebrisSlot b = pool.Acquire(2, 2.0f).slot;
    pool.SetPriority(a, 9.0f);
    const DebrisPool::Acquired c = pool.Acquire(3, 5.0f);
    ASSERT_TRUE(c.evicted.has_value());
    EXPECT_EQ(*c.evicted, 2u);
    EXPECT_TRUE(pool.IsLive(a));
    EXPECT_FALSE(pool.IsLive(b));
}

TEST(DebrisPool, AtCap_TiedPriorities_EvictsLowestSlot)
{
    DebrisPool pool(3);
    for (std::uint64_t user = 0; user < 3; ++user)
    {
        (void)pool.Acquire(user, 1.0f);
    }
    const DebrisPool::Acquired next = pool.Acquire(7, 1.0f);
    ASSERT_TRUE(next.evicted.has_value());
    EXPECT_EQ(*next.evicted, 0u);
    EXPECT_EQ(next.slot.index, 0u);
}

TEST(DebrisPool, AtCap_NewcomerLowest_Rejected)
{
    DebrisPool pool(2);
    (void)pool.Acquire(1, 2.0f);
    (void)pool.Acquire(2, 3.0f);
    const DebrisPool::Acquired rejected = pool.Acquire(3, 1.0f);
    EXPECT_FALSE(rejected.slot.IsValid());
    EXPECT_FALSE(rejected.evicted.has_value());
    EXPECT_EQ(pool.LiveCount(), 2u);
}

TEST(DebrisPool, ZeroCapacity_RejectsEverything)
{
    DebrisPool pool(0);
    EXPECT_FALSE(pool.Acquire(1, 1.0f).slot.IsValid());
}

TEST(DebrisPool, Release_FreesSlotAndStalesHandle)
{
    DebrisPool pool(1);
    const DebrisSlot a = pool.Acquire(1, 1.0f).slot;
    EXPECT_TRUE(pool.Release(a));
    EXPECT_FALSE(pool.IsLive(a));
    EXPECT_FALSE(pool.Release(a));
    EXPECT_EQ(pool.LiveCount(), 0u);

    const DebrisPool::Acquired b = pool.Acquire(2, 1.0f);
    EXPECT_FALSE(b.evicted.has_value());
    EXPECT_EQ(b.slot.index, a.index);
    EXPECT_NE(b.slot.generation, a.generation);
    EXPECT_FALSE(pool.IsLive(a));
}

TEST(DebrisPool, ForEachLive_VisitsSlotsInIndexOrder)
{
    DebrisPool pool(4);
    const DebrisSlot a = pool.Acquire(10, 1.0f).slot;
    (void)pool.Acquire(11, 1.0f);
    (void)pool.Acquire(12, 1.0f);
    ASSERT_TRUE(pool.Release(a));
    std::vector<std::uint64_t> users;
    pool.ForEachLive([&](DebrisSlot slot) { users.push_back(pool.User(slot)); });
    ASSERT_EQ(users.size(), 2u);
    EXPECT_LT(users[0], users[1]);
}

TEST(DebrisPool, Recycle_NoAllocation)
{
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Destruction);
    const std::uint64_t before = interi::GetMemoryStats(interi::MemoryTag::Destruction).allocationCount;
    {
        DebrisPool pool(64, tracked);
        const std::uint64_t constructed = interi::GetMemoryStats(interi::MemoryTag::Destruction).allocationCount;
        EXPECT_GT(constructed, before);

        std::vector<DebrisSlot> live;
        live.reserve(64);
        for (std::uint64_t user = 0; user < 10'000; ++user)
        {
            const DebrisPool::Acquired acquired = pool.Acquire(user, static_cast<float>(user % 17));
            if (acquired.slot.IsValid() && user % 3 == 0)
            {
                live.push_back(acquired.slot);
            }
            if (live.size() == 32)
            {
                for (const DebrisSlot slot : live)
                {
                    (void)pool.Release(slot);
                }
                live.clear();
            }
        }
        EXPECT_EQ(interi::GetMemoryStats(interi::MemoryTag::Destruction).allocationCount, constructed);
    }
}

} // namespace

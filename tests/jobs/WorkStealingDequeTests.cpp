// SPDX-License-Identifier: MIT
#include <interi/jobs/WorkStealingDeque.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

using interi::WorkStealingDeque;

TEST(WorkStealingDeque, Empty_PopAndStealReturnNothing)
{
    WorkStealingDeque<std::uint64_t> deque(8);
    EXPECT_FALSE(deque.Pop().has_value());
    EXPECT_FALSE(deque.Steal().has_value());
    EXPECT_EQ(deque.SizeApprox(), 0u);
}

TEST(WorkStealingDeque, Capacity_RoundedUpToPowerOfTwo)
{
    WorkStealingDeque<std::uint64_t> deque(5);
    EXPECT_EQ(deque.Capacity(), 8u);
}

TEST(WorkStealingDeque, OwnerPop_IsLifo)
{
    WorkStealingDeque<std::uint64_t> deque(8);
    for (std::uint64_t i = 1; i <= 3; ++i)
    {
        ASSERT_TRUE(deque.Push(i));
    }
    EXPECT_EQ(deque.Pop(), std::optional<std::uint64_t>(3));
    EXPECT_EQ(deque.Pop(), std::optional<std::uint64_t>(2));
    EXPECT_EQ(deque.Pop(), std::optional<std::uint64_t>(1));
    EXPECT_FALSE(deque.Pop().has_value());
}

TEST(WorkStealingDeque, Steal_IsFifo)
{
    WorkStealingDeque<std::uint64_t> deque(8);
    for (std::uint64_t i = 1; i <= 3; ++i)
    {
        ASSERT_TRUE(deque.Push(i));
    }
    EXPECT_EQ(deque.Steal(), std::optional<std::uint64_t>(1));
    EXPECT_EQ(deque.Steal(), std::optional<std::uint64_t>(2));
    EXPECT_EQ(deque.Pop(), std::optional<std::uint64_t>(3));
    EXPECT_FALSE(deque.Steal().has_value());
}

TEST(WorkStealingDeque, PushWhenFull_Fails)
{
    WorkStealingDeque<std::uint64_t> deque(4);
    for (std::uint64_t i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(deque.Push(i));
    }
    EXPECT_FALSE(deque.Push(99));
    EXPECT_EQ(deque.SizeApprox(), 4u);
    EXPECT_TRUE(deque.Steal().has_value());
    EXPECT_TRUE(deque.Push(99)); // stealing freed a slot
}

TEST(WorkStealingDeque, ManyLaps_WrapAroundKeepsOrder)
{
    WorkStealingDeque<std::uint64_t> deque(4);
    std::uint64_t next = 0;
    std::uint64_t expectedSteal = 0;
    for (int lap = 0; lap < 100; ++lap)
    {
        ASSERT_TRUE(deque.Push(next++));
        ASSERT_TRUE(deque.Push(next++));
        EXPECT_EQ(deque.Steal(), std::optional<std::uint64_t>(expectedSteal));
        EXPECT_EQ(deque.Pop(), std::optional<std::uint64_t>(expectedSteal + 1));
        expectedSteal += 2;
    }
    EXPECT_EQ(deque.SizeApprox(), 0u);
}

// SPDX-License-Identifier: MIT
#include <interi/core/containers/MPMCQueue.h>
#include <interi/core/containers/SPSCQueue.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

TEST(SPSCQueue, PushPop_IsFifo)
{
    interi::SPSCQueue<int> queue(4);
    EXPECT_TRUE(queue.TryPush(1));
    EXPECT_TRUE(queue.TryPush(2));
    int value = 0;
    ASSERT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, 1);
    ASSERT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, 2);
    EXPECT_FALSE(queue.TryPop(value));
}

TEST(SPSCQueue, Full_RejectsPush)
{
    interi::SPSCQueue<int> queue(4);
    EXPECT_EQ(queue.Capacity(), 4u);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(queue.TryPush(i));
    }
    EXPECT_FALSE(queue.TryPush(99));
    int value = 0;
    ASSERT_TRUE(queue.TryPop(value));
    EXPECT_TRUE(queue.TryPush(4)) << "space is reusable after a pop";
}

TEST(MPMCQueue, PushPop_IsFifoForOneThread)
{
    interi::MPMCQueue<std::string> queue(8);
    EXPECT_TRUE(queue.TryPush("a"));
    EXPECT_TRUE(queue.TryPush("b"));
    std::string value;
    ASSERT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, "a");
    ASSERT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, "b");
    EXPECT_FALSE(queue.TryPop(value));
}

TEST(MPMCQueue, Full_RejectsPushAndWrapsAround)
{
    interi::MPMCQueue<int> queue(3); // rounded up to 4
    EXPECT_EQ(queue.Capacity(), 4u);
    for (int round = 0; round < 3; ++round)
    {
        for (int i = 0; i < 4; ++i)
        {
            ASSERT_TRUE(queue.TryPush(i));
        }
        EXPECT_FALSE(queue.TryPush(4));
        for (int i = 0; i < 4; ++i)
        {
            int value = -1;
            ASSERT_TRUE(queue.TryPop(value));
            EXPECT_EQ(value, i);
        }
    }
}

TEST(MPMCQueue, MoveOnlyElements_AreSupportedAndDestroyed)
{
    auto counter = std::make_shared<int>(0);
    {
        interi::MPMCQueue<std::shared_ptr<int>> queue(4);
        EXPECT_TRUE(queue.TryPush(counter));
        EXPECT_TRUE(queue.TryPush(counter));
        EXPECT_EQ(counter.use_count(), 3);
        std::shared_ptr<int> out;
        ASSERT_TRUE(queue.TryPop(out));
        EXPECT_EQ(out, counter);
    } // queue destructor releases the element still inside
    EXPECT_EQ(counter.use_count(), 1);
}

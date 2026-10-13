// SPDX-License-Identifier: MIT
#include <interi/jobs/WorkStealingDeque.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

// The owner pushes every item (popping some back itself) while thieves steal concurrently. Every item must be taken
// exactly once, by either side.
TEST(WorkStealingDequeStress, OwnerPushPopThievesSteal_EachItemOnce)
{
    constexpr int kThieves = 6;
    constexpr std::uint64_t kItems = 400'000;

    interi::WorkStealingDeque<std::uint64_t> deque(256);
    auto seen = std::make_unique<std::atomic<std::uint8_t>[]>(kItems);
    std::atomic<std::uint64_t> taken{0};
    std::atomic<bool> ownerDone{false};

    const auto take = [&](std::uint64_t item)
    {
        seen[item].fetch_add(1, std::memory_order_relaxed);
        taken.fetch_add(1, std::memory_order_relaxed);
    };

    std::vector<std::jthread> thieves;
    for (int t = 0; t < kThieves; ++t)
    {
        thieves.emplace_back(
            [&]
            {
                while (!ownerDone.load(std::memory_order_acquire) || deque.SizeApprox() != 0)
                {
                    if (const auto item = deque.Steal())
                    {
                        take(*item);
                    }
                }
            });
    }

    for (std::uint64_t i = 0; i < kItems; ++i)
    {
        while (!deque.Push(i))
        {
            if (const auto item = deque.Pop())
            {
                take(*item);
            }
        }
        if (i % 3 == 0)
        {
            if (const auto item = deque.Pop())
            {
                take(*item);
            }
        }
    }
    while (const auto item = deque.Pop())
    {
        take(*item);
    }
    ownerDone.store(true, std::memory_order_release);
    thieves.clear();

    EXPECT_EQ(taken.load(), kItems);
    std::uint64_t wrong = 0;
    for (std::uint64_t i = 0; i < kItems; ++i)
    {
        wrong += seen[i].load() == 1 ? 0 : 1;
    }
    EXPECT_EQ(wrong, 0u);
}

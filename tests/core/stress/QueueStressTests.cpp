// SPDX-License-Identifier: MIT
#include <interi/core/containers/MPMCQueue.h>
#include <interi/core/containers/SPSCQueue.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

TEST(MPMCQueueStress, NProducersMConsumers_AllItemsExactlyOnce)
{
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr std::uint32_t kItemsPerProducer = 100'000;
    constexpr std::size_t kTotal = static_cast<std::size_t>(kProducers) * kItemsPerProducer;

    interi::MPMCQueue<std::uint64_t> queue(1024);
    auto seen = std::make_unique<std::atomic<std::uint8_t>[]>(kTotal);
    std::atomic<std::size_t> consumed{0};
    std::atomic<int> duplicates{0};

    std::vector<std::jthread> threads;
    for (int p = 0; p < kProducers; ++p)
    {
        threads.emplace_back(
            [&queue, p]
            {
                for (std::uint32_t i = 0; i < kItemsPerProducer; ++i)
                {
                    const std::uint64_t item = static_cast<std::uint64_t>(p) * kItemsPerProducer + i;
                    while (!queue.TryPush(item))
                    {
                        std::this_thread::yield();
                    }
                }
            });
    }
    for (int c = 0; c < kConsumers; ++c)
    {
        threads.emplace_back(
            [&]
            {
                std::uint64_t item = 0;
                while (consumed.load(std::memory_order_relaxed) < kTotal)
                {
                    if (queue.TryPop(item))
                    {
                        if (seen[item].fetch_add(1, std::memory_order_relaxed) != 0)
                        {
                            duplicates.fetch_add(1);
                        }
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            });
    }
    threads.clear();

    EXPECT_EQ(duplicates.load(), 0);
    std::size_t missing = 0;
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        missing += seen[i].load() == 0 ? 1 : 0;
    }
    EXPECT_EQ(missing, 0u);
}

TEST(SPSCQueueStress, ProducerConsumer_PreservesOrder)
{
    constexpr std::uint32_t kItems = 1'000'000;
    interi::SPSCQueue<std::uint32_t> queue(256);
    bool inOrder = true;

    std::jthread consumer(
        [&]
        {
            std::uint32_t expected = 0;
            std::uint32_t item = 0;
            while (expected < kItems)
            {
                if (queue.TryPop(item))
                {
                    inOrder = inOrder && item == expected;
                    ++expected;
                }
            }
        });
    for (std::uint32_t i = 0; i < kItems; ++i)
    {
        while (!queue.TryPush(i))
        {
        }
    }
    consumer.join();
    EXPECT_TRUE(inOrder);
}

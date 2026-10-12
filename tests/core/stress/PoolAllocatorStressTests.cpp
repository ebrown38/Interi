// SPDX-License-Identifier: MIT
#include <interi/core/memory/PoolAllocator.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

TEST(PoolAllocatorStress, ConcurrentAllocFree_NoDoubleHandout)
{
    constexpr std::size_t kBlocks = 64;
    constexpr int kThreads = 8;
    constexpr int kIterations = 50'000;

    interi::PoolAllocator pool(sizeof(std::uint64_t) * 4, kBlocks);
    // Each block's owner flag: a double handout shows up as a failed 0 -> 1 exchange.
    auto owned = std::make_unique<std::atomic<int>[]>(kBlocks);
    std::atomic<int> doubleHandouts{0};
    std::atomic<int> corrupted{0};

    std::vector<std::jthread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&, t]
            {
                std::vector<std::uint64_t*> held;
                for (int i = 0; i < kIterations; ++i)
                {
                    if (held.size() < 4 && (i % 3) != 2)
                    {
                        auto* block = static_cast<std::uint64_t*>(pool.Allocate(sizeof(std::uint64_t) * 4, 8));
                        if (block == nullptr)
                        {
                            continue;
                        }
                        const std::size_t index = pool.IndexOf(block);
                        if (owned[index].exchange(1, std::memory_order_acq_rel) != 0)
                        {
                            doubleHandouts.fetch_add(1);
                        }
                        const std::uint64_t stamp = (static_cast<std::uint64_t>(t) << 32) | static_cast<unsigned>(i);
                        for (int w = 0; w < 4; ++w)
                        {
                            block[w] = stamp;
                        }
                        held.push_back(block);
                    }
                    else if (!held.empty())
                    {
                        std::uint64_t* block = held.back();
                        held.pop_back();
                        for (int w = 1; w < 4; ++w)
                        {
                            if (block[w] != block[0])
                            {
                                corrupted.fetch_add(1);
                            }
                        }
                        owned[pool.IndexOf(block)].store(0, std::memory_order_release);
                        pool.Free(block, sizeof(std::uint64_t) * 4);
                    }
                }
                for (std::uint64_t* block : held)
                {
                    owned[pool.IndexOf(block)].store(0, std::memory_order_release);
                    pool.Free(block, sizeof(std::uint64_t) * 4);
                }
            });
    }
    threads.clear();

    EXPECT_EQ(doubleHandouts.load(), 0);
    EXPECT_EQ(corrupted.load(), 0);
    EXPECT_EQ(pool.FreeCount(), kBlocks) << "every block returned";
}

// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using interi::JobCounter;
using interi::JobPriority;
using interi::JobSystem;
using interi::JobSystemDesc;

TEST(JobSystemStress, ExternalSubmittersAndNestedJobs_AllRun)
{
    JobSystemDesc desc;
    desc.workerCount = 6;
    JobSystem jobs(desc);

    constexpr int kSubmitters = 4;
    constexpr int kJobsPerSubmitter = 20'000;
    std::atomic<std::int64_t> ran{0};
    std::vector<std::jthread> submitters;
    for (int s = 0; s < kSubmitters; ++s)
    {
        submitters.emplace_back(
            [&jobs, &ran, s]
            {
                JobCounter counter;
                for (int i = 0; i < kJobsPerSubmitter; ++i)
                {
                    const auto priority = static_cast<JobPriority>(static_cast<std::size_t>(i + s) % 2); // High/Normal
                    jobs.Submit(
                        [&jobs, &ran, &counter, i]
                        {
                            ran.fetch_add(1, std::memory_order_relaxed);
                            if (i % 16 == 0)
                            {
                                jobs.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); }, &counter);
                            }
                        },
                        &counter, priority);
                }
                jobs.WaitFor(counter);
            });
    }
    submitters.clear();

    constexpr std::int64_t kNested = (kJobsPerSubmitter + 15) / 16;
    EXPECT_EQ(ran.load(), static_cast<std::int64_t>(kSubmitters) * (kJobsPerSubmitter + kNested));
}

TEST(JobSystemStress, RepeatedCreateSubmitDestroy_DrainsEveryTime)
{
    for (int round = 0; round < 200; ++round)
    {
        std::atomic<int> ran{0};
        {
            JobSystemDesc desc;
            desc.workerCount = static_cast<std::uint32_t>(round % 5);
            JobSystem jobs(desc);
            for (int i = 0; i < 100; ++i)
            {
                jobs.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); }, nullptr,
                            static_cast<JobPriority>(static_cast<std::size_t>(i) % interi::kJobPriorityCount));
            }
        }
        ASSERT_EQ(ran.load(), 100) << "round " << round;
    }
}

namespace
{

// Every non-leaf job submits kFanOut children and waits for them, so threads that wait run other waiting jobs
// (nested waits). Depth stays below kMaxWaitNesting; deeper chains are documented as unsupported.
struct NestedTree
{
    static constexpr int kFanOut = 4;
    static constexpr int kDepth = 8;

    JobSystem& jobs;
    std::atomic<std::int64_t> waits{0};
    std::atomic<std::int64_t> leaves{0};
    std::atomic<int> maxStackedVisits{0}; ///< Most Visit calls ever active at once on one thread's stack.

    static inline thread_local int t_stackedVisits = 0;

    void Visit(int depth)
    {
        const int stacked = ++t_stackedVisits;
        int seen = maxStackedVisits.load(std::memory_order_relaxed);
        while (stacked > seen && !maxStackedVisits.compare_exchange_weak(seen, stacked, std::memory_order_relaxed))
        {
        }
        VisitBody(depth);
        --t_stackedVisits;
    }

    void VisitBody(int depth)
    {
        if (depth == kDepth)
        {
            leaves.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        JobCounter children;
        for (int i = 0; i < kFanOut; ++i)
        {
            jobs.Submit([this, depth] { Visit(depth + 1); }, &children);
        }
        jobs.WaitFor(children);
        waits.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace

TEST(JobSystemStress, WaitFor_NestedJobsWaiting_NoDeadlock)
{
    static_assert(NestedTree::kDepth < static_cast<int>(JobSystem::kMaxWaitNesting));
    for (const std::uint32_t workers : {0u, 1u, 7u, 31u})
    {
        JobSystemDesc desc;
        desc.workerCount = workers;
        desc.maxJobs = 4096; // smaller than the tree, so the pool-exhausted inline path runs too
        JobSystem jobs(desc);
        NestedTree tree{jobs};
        tree.Visit(0);

        std::int64_t expectedWaits = 0;
        std::int64_t level = 1;
        for (int d = 0; d < NestedTree::kDepth; ++d)
        {
            expectedWaits += level;
            level *= NestedTree::kFanOut;
        }
        EXPECT_EQ(tree.waits.load(), expectedWaits) << workers << " workers"; // 21845 nested waits
        EXPECT_EQ(tree.leaves.load(), level) << workers << " workers";
        // Stack depth must stay bounded (Windows threads have 1 MB stacks). A waiter that picked up unrelated older
        // jobs instead of its own children would nest roughly once per queued subtree.
        EXPECT_LE(tree.maxStackedVisits.load(), static_cast<int>(JobSystem::kMaxWaitNesting) + NestedTree::kDepth + 1)
            << workers << " workers";
    }
}

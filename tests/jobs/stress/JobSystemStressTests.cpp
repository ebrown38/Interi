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

// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/jobs_jolt/JoltJobSystem.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using interi::JobSystem;
using interi::JobSystemDesc;
using interi::JoltJobSystem;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// Creates a barrier, runs `body(barrier)` to add jobs, waits for the barrier and destroys it.
template <typename F>
void WithBarrier(JoltJobSystem& jolt, F&& body)
{
    JPH::JobSystem::Barrier* barrier = jolt.CreateBarrier();
    body(*barrier);
    jolt.WaitForJobs(barrier);
    jolt.DestroyBarrier(barrier);
}

} // namespace

TEST(JoltJobAdapter, JoltBuildConfiguration_MatchesHeaders)
{
    EXPECT_TRUE(JPH::VerifyJoltVersionID()); // a define mismatch would silently change class layouts
}

TEST(JoltJobAdapter, MaxConcurrency_IsThreadCount)
{
    JobSystem jobs(Workers(5));
    JoltJobSystem jolt(jobs);
    EXPECT_EQ(jolt.GetMaxConcurrency(), 6);
}

class JoltJobAdapterWorkers : public ::testing::TestWithParam<std::uint32_t>
{
};

TEST_P(JoltJobAdapterWorkers, BarrierWaitsForAllJobs)
{
    JobSystem jobs(Workers(GetParam()));
    JoltJobSystem jolt(jobs);
    for (int round = 0; round < 20; ++round)
    {
        constexpr int kJobs = 200;
        std::atomic<int> finished{0};
        WithBarrier(jolt,
                    [&](JPH::JobSystem::Barrier& barrier)
                    {
                        std::vector<JPH::JobHandle> handles;
                        for (int i = 0; i < kJobs; ++i)
                        {
                            handles.push_back(jolt.CreateJob("Count", JPH::Color::sGreen,
                                                             [&finished]
                                                             {
                                                                 std::this_thread::yield();
                                                                 finished.fetch_add(1);
                                                             }));
                        }
                        barrier.AddJobs(handles.data(), static_cast<JPH::uint>(handles.size()));
                    });
        ASSERT_EQ(finished.load(), kJobs) << "round " << round;
    }
}

TEST_P(JoltJobAdapterWorkers, JobDependencies_Respected)
{
    JobSystem jobs(Workers(GetParam()));
    JoltJobSystem jolt(jobs);
    for (int round = 0; round < 50; ++round)
    {
        // Diamond: first -> {left, right} -> last. Each dependent job starts only when its dependency count is 0.
        std::atomic<int> clock{0};
        std::atomic<int> firstAt{-1};
        std::atomic<int> leftAt{-1};
        std::atomic<int> rightAt{-1};
        std::atomic<int> lastAt{-1};
        WithBarrier(jolt,
                    [&](JPH::JobSystem::Barrier& barrier)
                    {
                        const JPH::JobHandle last =
                            jolt.CreateJob("Last", JPH::Color::sRed, [&] { lastAt = clock.fetch_add(1); }, 2);
                        const JPH::JobHandle left = jolt.CreateJob(
                            "Left", JPH::Color::sRed,
                            [&, last]
                            {
                                leftAt = clock.fetch_add(1);
                                last.RemoveDependency();
                            },
                            1);
                        const JPH::JobHandle right = jolt.CreateJob(
                            "Right", JPH::Color::sRed,
                            [&, last]
                            {
                                rightAt = clock.fetch_add(1);
                                last.RemoveDependency();
                            },
                            1);
                        const JPH::JobHandle first = jolt.CreateJob("First", JPH::Color::sRed,
                                                                    [&, left, right]
                                                                    {
                                                                        firstAt = clock.fetch_add(1);
                                                                        left.RemoveDependency();
                                                                        right.RemoveDependency();
                                                                    });
                        const JPH::JobHandle all[] = {first, left, right, last};
                        barrier.AddJobs(all, 4);
                    });
        ASSERT_EQ(clock.load(), 4) << "round " << round;
        EXPECT_LT(firstAt.load(), leftAt.load());
        EXPECT_LT(firstAt.load(), rightAt.load());
        EXPECT_LT(leftAt.load(), lastAt.load());
        EXPECT_LT(rightAt.load(), lastAt.load());
    }
}

INSTANTIATE_TEST_SUITE_P(Workers, JoltJobAdapterWorkers, ::testing::Values(0u, 1u, 7u));

TEST(JoltJobAdapter, Destructor_WaitsForQueuedJobs)
{
    JobSystem jobs(Workers(2));
    std::atomic<int> ran{0};
    {
        JoltJobSystem jolt(jobs);
        for (int i = 0; i < 100; ++i)
        {
            // Not added to any barrier: the adapter must still keep each job alive until its Interi job has run.
            const JPH::JobHandle handle = jolt.CreateJob("Loose", JPH::Color::sBlue, [&ran] { ran.fetch_add(1); });
            (void)handle;
        }
    }
    EXPECT_EQ(ran.load(), 100);
}

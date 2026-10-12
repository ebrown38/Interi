// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

using interi::JobCounter;
using interi::JobPriority;
using interi::JobSystem;
using interi::JobSystemDesc;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

void SpinUntil(const std::atomic<bool>& flag)
{
    while (!flag.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

} // namespace

TEST(JobSystem, DefaultWorkerCount_IsHardwareThreadsMinusOne)
{
    const std::uint32_t hardware = std::max(std::thread::hardware_concurrency(), 2u);
    EXPECT_EQ(JobSystem::DefaultWorkerCount(), hardware - 1);

    JobSystem jobs;
    EXPECT_EQ(jobs.WorkerCount(), JobSystem::DefaultWorkerCount());
    EXPECT_EQ(jobs.ThreadCount(), jobs.WorkerCount() + 1);
}

TEST(JobSystem, SubmitN_AllRun)
{
    JobSystem jobs(Workers(4));
    constexpr int kJobs = 10'000;
    std::atomic<int> ran{0};
    JobCounter counter;
    for (int i = 0; i < kJobs; ++i)
    {
        jobs.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); }, &counter);
    }
    jobs.WaitFor(counter);
    EXPECT_TRUE(counter.IsDone());
    EXPECT_EQ(ran.load(), kJobs);
}

TEST(JobSystem, ZeroWorkers_WaitForRunsJobsOnCaller)
{
    JobSystem jobs(Workers(0));
    EXPECT_EQ(jobs.ThreadCount(), 1u);
    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<int> ranOnCaller{0};
    JobCounter counter;
    for (int i = 0; i < 100; ++i)
    {
        const JobPriority priority = static_cast<JobPriority>(static_cast<std::size_t>(i) % interi::kJobPriorityCount);
        jobs.Submit(
            [&]
            {
                if (std::this_thread::get_id() == caller)
                {
                    ranOnCaller.fetch_add(1);
                }
            },
            &counter, priority);
    }
    EXPECT_EQ(counter.Pending(), 100);
    jobs.WaitFor(counter);
    EXPECT_EQ(ranOnCaller.load(), 100);
}

TEST(JobSystem, JobsSubmittingJobs_AllRun)
{
    JobSystem jobs(Workers(4));
    std::atomic<int> leaves{0};
    JobCounter counter;
    for (int i = 0; i < 64; ++i)
    {
        jobs.Submit(
            [&]
            {
                for (int j = 0; j < 64; ++j)
                {
                    jobs.Submit([&leaves] { leaves.fetch_add(1, std::memory_order_relaxed); }, &counter);
                }
            },
            &counter);
    }
    jobs.WaitFor(counter);
    EXPECT_EQ(leaves.load(), 64 * 64);
}

TEST(JobSystem, WaitFor_FinishedCounter_ReturnsImmediately)
{
    JobSystem jobs(Workers(1));
    JobCounter counter;
    EXPECT_TRUE(counter.IsDone());
    jobs.WaitFor(counter);
    EXPECT_EQ(counter.Pending(), 0);
}

TEST(JobSystem, HighPriority_RunsBeforeLowWhenQueued)
{
    JobSystem jobs(Workers(1));
    std::atomic<bool> gateStarted{false};
    std::atomic<bool> gateOpen{false};
    std::mutex orderMutex;
    std::vector<JobPriority> order;

    JobCounter counter;
    jobs.Submit(
        [&]
        {
            gateStarted.store(true, std::memory_order_release);
            SpinUntil(gateOpen);
        },
        &counter, JobPriority::High);
    SpinUntil(gateStarted); // the only worker is now busy, so everything below stays queued

    const auto record = [&](JobPriority priority)
    {
        return [&order, &orderMutex, priority]
        {
            const std::scoped_lock lock(orderMutex);
            order.push_back(priority);
        };
    };
    for (int i = 0; i < 4; ++i)
    {
        jobs.Submit(record(JobPriority::Low), &counter, JobPriority::Low);
        jobs.Submit(record(JobPriority::Normal), &counter, JobPriority::Normal);
        jobs.Submit(record(JobPriority::High), &counter, JobPriority::High);
    }
    gateOpen.store(true, std::memory_order_release);
    while (!counter.IsDone()) // don't help: the caller would race the worker and blur the order
    {
        std::this_thread::yield();
    }

    ASSERT_EQ(order.size(), 12u);
    EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
}

TEST(JobSystem, Shutdown_DrainsOrCancelsCleanly)
{
    std::atomic<int> ran{0};
    {
        JobSystem jobs(Workers(4));
        for (int i = 0; i < 256; ++i)
        {
            jobs.Submit(
                [&]
                {
                    ran.fetch_add(1, std::memory_order_relaxed);
                    for (int j = 0; j < 8; ++j)
                    {
                        jobs.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
                    }
                },
                nullptr, static_cast<JobPriority>(static_cast<std::size_t>(i) % interi::kJobPriorityCount));
        }
        // No wait: the destructor must run every submitted job, including ones submitted while draining.
    }
    EXPECT_EQ(ran.load(), 256 * 9);
}

TEST(JobSystem, PoolExhausted_SubmitRunsInline)
{
    JobSystemDesc desc = Workers(1);
    desc.maxJobs = 4;
    JobSystem jobs(desc);

    std::atomic<bool> gateStarted{false};
    std::atomic<bool> gateOpen{false};
    JobCounter counter;
    jobs.Submit(
        [&]
        {
            gateStarted.store(true, std::memory_order_release);
            SpinUntil(gateOpen);
        },
        &counter);
    SpinUntil(gateStarted);
    for (int i = 0; i < 3; ++i)
    {
        jobs.Submit([] {}, &counter); // fills the pool (the gate job still holds one slot)
    }

    std::optional<std::thread::id> ranOn;
    jobs.Submit([&ranOn] { ranOn = std::this_thread::get_id(); }, &counter);
    ASSERT_TRUE(ranOn.has_value()); // ran synchronously inside Submit
    EXPECT_EQ(*ranOn, std::this_thread::get_id());

    gateOpen.store(true, std::memory_order_release);
    jobs.WaitFor(counter);
}

TEST(JobSystem, CurrentWorkerIndex_OnlyOnWorkers)
{
    JobSystem jobs(Workers(3));
    EXPECT_FALSE(jobs.CurrentWorkerIndex().has_value());

    std::atomic<int> invalid{0};
    JobCounter counter;
    for (int i = 0; i < 200; ++i)
    {
        jobs.Submit(
            [&]
            {
                const std::optional<std::uint32_t> index = jobs.CurrentWorkerIndex();
                if (index.has_value() && *index >= jobs.WorkerCount())
                {
                    invalid.fetch_add(1);
                }
            },
            &counter);
    }
    jobs.WaitFor(counter);
    EXPECT_EQ(invalid.load(), 0);

    JobSystem other(Workers(1)); // a worker of one system is not a worker of another
    std::atomic<bool> seenAsWorker{true};
    JobCounter otherCounter;
    other.Submit([&] { seenAsWorker = jobs.CurrentWorkerIndex().has_value(); }, &otherCounter);
    other.WaitFor(otherCounter);
    EXPECT_FALSE(seenAsWorker.load());
}

TEST(JobSystem, CapturedState_DestroyedAfterRun)
{
    struct Tracker
    {
        std::atomic<int>* destroyed;
        explicit Tracker(std::atomic<int>* counter)
            : destroyed(counter)
        {
        }
        Tracker(const Tracker&) = delete;
        Tracker& operator=(const Tracker&) = delete;
        Tracker(Tracker&& other) noexcept
            : destroyed(std::exchange(other.destroyed, nullptr))
        {
        }
        Tracker& operator=(Tracker&&) = delete;
        ~Tracker()
        {
            if (destroyed != nullptr)
            {
                destroyed->fetch_add(1);
            }
        }
    };

    std::atomic<int> destroyed{0};
    {
        JobSystem jobs(Workers(2));
        JobCounter counter;
        for (int i = 0; i < 50; ++i)
        {
            jobs.Submit([tracker = Tracker(&destroyed)] { (void)tracker; }, &counter);
        }
        jobs.WaitFor(counter);
        EXPECT_EQ(destroyed.load(), 50);
    }
    EXPECT_EQ(destroyed.load(), 50);
}

#if defined(__linux__)
TEST(JobSystem, PinThreads_EachWorkerOnOneCore)
{
    JobSystemDesc desc = Workers(2);
    desc.pinThreads = true;
    JobSystem jobs(desc);

    std::atomic<int> unpinned{0};
    std::atomic<int> onWorker{0};
    JobCounter counter;
    for (int i = 0; i < 200; ++i)
    {
        jobs.Submit(
            [&]
            {
                if (!jobs.CurrentWorkerIndex().has_value())
                {
                    return;
                }
                onWorker.fetch_add(1);
                cpu_set_t set;
                CPU_ZERO(&set);
                pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
                if (CPU_COUNT(&set) != 1)
                {
                    unpinned.fetch_add(1);
                }
            },
            &counter);
    }
    jobs.WaitFor(counter);
    EXPECT_EQ(unpinned.load(), 0);
    (void)onWorker; // the caller may have run every job while waiting; nothing to assert about the split
}
#endif

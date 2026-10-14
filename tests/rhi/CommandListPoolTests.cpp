// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/rhi/CommandListPool.h>

#include "MockDevice.h"
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

using interi::JobSystem;
using interi::JobSystemDesc;
using interi::rhi::CommandListPool;
using interi::rhi::CommandPoolHandle;
using interi::rhi::ICommandList;
using interi::testing::MockCommandList;
using interi::testing::MockDevice;
using ::testing::_;
using ::testing::NiceMock;

namespace
{

/// A device whose command pools are counted handles; it records which thread begins lists from which pool.
struct PoolFixture : ::testing::Test
{
    PoolFixture()
    {
        ON_CALL(device, CreateCommandPool())
            .WillByDefault(
                [this]
                {
                    std::scoped_lock lock(mutex);
                    return interi::Result<CommandPoolHandle>(CommandPoolHandle::FromParts(nextPool++, 1));
                });
        ON_CALL(device, BeginCommandList(_))
            .WillByDefault(
                [this](CommandPoolHandle pool) -> ICommandList&
                {
                    std::scoped_lock lock(mutex);
                    users[pool.Index()].insert(std::this_thread::get_id());
                    return lists.emplace_back();
                });
    }

    std::mutex mutex;
    std::uint32_t nextPool = 0;
    std::map<std::uint32_t, std::set<std::thread::id>> users;
    std::deque<NiceMock<MockCommandList>> lists;
    NiceMock<MockDevice> device;
};

} // namespace

TEST_F(PoolFixture, CommandListPool_PerThread_NoSharing)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 4});
    auto pool = CommandListPool::Create(device, jobs, 2);
    ASSERT_TRUE(pool.has_value());
    // One pool per thread (4 workers + the owning thread) per frame in flight.
    EXPECT_EQ(nextPool, 10u);

    pool->BeginFrame();
    interi::ParallelFor(jobs, 0, 2000, 1, [&](std::size_t) { (void)pool->Acquire(); });
    (void)pool->Acquire();

    for (const auto& [index, threads] : users)
    {
        EXPECT_EQ(threads.size(), 1u) << "pool " << index << " was recorded from several threads";
    }
    EXPECT_GE(users.size(), 1u);
}

TEST_F(PoolFixture, CommandListPool_ThreadsOfOneFrame_UseThatFramesPools)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 0});
    auto pool = CommandListPool::Create(device, jobs, 2);
    ASSERT_TRUE(pool.has_value());

    pool->BeginFrame();
    (void)pool->Acquire();
    pool->EndFrame(1);
    pool->BeginFrame();
    (void)pool->Acquire();
    pool->EndFrame(2);

    // Frames alternate between the two per-frame pools of the owning thread.
    ASSERT_EQ(users.size(), 2u);
    EXPECT_NE(users.begin()->first, std::next(users.begin())->first);
}

TEST_F(PoolFixture, CommandListPool_BeginFrame_WaitsForSlotFenceThenResets)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 1});
    auto pool = CommandListPool::Create(device, jobs, 2);
    ASSERT_TRUE(pool.has_value());

    pool->BeginFrame(); // slot 0, never submitted: nothing to wait for
    pool->EndFrame(5);
    pool->BeginFrame(); // slot 1
    pool->EndFrame(6);

    // Reusing slot 0 must wait for its last submission before recycling its two pools (one worker + owner).
    {
        ::testing::InSequence sequence;
        EXPECT_CALL(device, WaitForValue(5)).Times(1);
        EXPECT_CALL(device, ResetCommandPool(_)).Times(2);
    }
    pool->BeginFrame();
    ::testing::Mock::VerifyAndClearExpectations(&device);

    // Destruction waits for the newest submission, then frees all four pools.
    pool->EndFrame(7);
    EXPECT_CALL(device, WaitForValue(7)).Times(1);
    EXPECT_CALL(device, DestroyCommandPool(_)).Times(4);
}

TEST_F(PoolFixture, CommandListPool_AcquireFromForeignThread_Asserts)
{
    if (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts disabled";
    }
    JobSystem jobs(JobSystemDesc{.workerCount = 0});
    auto pool = CommandListPool::Create(device, jobs, 2);
    ASSERT_TRUE(pool.has_value());
    pool->BeginFrame();

    static std::atomic<bool> asserted{false};
    asserted = false;
    {
        const interi::ScopedAssertHandler handler(
            [](const interi::AssertInfo&)
            {
                asserted = true;
                return interi::AssertAction::Continue;
            });
        std::thread([&] { (void)pool->Acquire(); }).join();
    }
    EXPECT_TRUE(asserted);
}

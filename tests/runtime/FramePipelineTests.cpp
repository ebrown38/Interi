// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/runtime/FramePipeline.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using interi::FramePipeline;
using interi::JobSystem;
using interi::JobSystemDesc;

namespace
{

struct Snapshot
{
    std::uint64_t frame = 0;
    std::vector<std::uint64_t> values;
};

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

} // namespace

TEST(FramePipeline, FirstFrame_HasNothingToRender)
{
    JobSystem jobs(Workers(2));
    FramePipeline<Snapshot> pipeline(jobs);
    int rendered = 0;
    pipeline.RunFrame([](Snapshot& out) { out.frame = 1; }, [&](const Snapshot&) { ++rendered; });
    EXPECT_EQ(rendered, 0);
    pipeline.RunFrame([](Snapshot& out) { out.frame = 2; },
                      [&](const Snapshot& in)
                      {
                          ++rendered;
                          EXPECT_EQ(in.frame, 1u);
                      });
    EXPECT_EQ(rendered, 1);
    EXPECT_EQ(pipeline.Latest().frame, 2u);
}

TEST(FramePipeline, RenderPrep_SeesPreviousFrameWhileSimRuns)
{
    JobSystem jobs(Workers(2));
    FramePipeline<Snapshot> pipeline(jobs);
    std::atomic<int> inside{0};
    std::atomic<bool> overlapped{false};
    const auto meet = [&]
    {
        inside.fetch_add(1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (inside.load() < 2 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        overlapped = overlapped || inside.load() >= 2;
    };
    pipeline.RunFrame([](Snapshot& out) { out.frame = 1; }, [](const Snapshot&) {});
    pipeline.RunFrame(
        [&](Snapshot& out)
        {
            meet();
            out.frame = 2;
        },
        [&](const Snapshot& in)
        {
            meet();
            EXPECT_EQ(in.frame, 1u);
        });
    EXPECT_TRUE(overlapped.load());
}

TEST(FramePipeline, ZeroWorkers_StillRendersEveryFrame)
{
    JobSystem jobs(Workers(0));
    FramePipeline<Snapshot> pipeline(jobs);
    std::uint64_t lastRendered = 0;
    for (std::uint64_t frame = 1; frame <= 10; ++frame)
    {
        pipeline.RunFrame([frame](Snapshot& out) { out.frame = frame; },
                          [&](const Snapshot& in) { lastRendered = in.frame; });
    }
    EXPECT_EQ(lastRendered, 9u);
}

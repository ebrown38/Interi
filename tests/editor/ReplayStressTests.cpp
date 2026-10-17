// SPDX-License-Identifier: MIT
// Replays are bit-identical at any worker count (the physics and destruction underneath are).
#include <interi/editor/PlaySession.h>
#include <interi/jobs/JobSystem.h>

#include "PlayTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;
using interi::editor::testing::PlayLevel;

TEST(ReplayDeterminism, WorkerCounts_SameHash)
{
    const PlayLevel play;
    Replay replay;
    {
        JobSystemDesc desc;
        desc.workerCount = 1;
        JobSystem jobs(desc);
        auto session = PlaySession::Start(play.level, play.files, jobs);
        ASSERT_TRUE(session.has_value());
        for (int step = 0; step < 240; ++step)
        {
            if (step == 20 || step == 90)
            {
                (*session)->Detonate(PlayLevel::Blast());
            }
            (*session)->Step();
        }
        replay = (*session)->Recording("test");
    }
    for (const std::uint32_t workers : {1u, 4u, 32u})
    {
        JobSystemDesc desc;
        desc.workerCount = workers;
        JobSystem jobs(desc);
        const auto hash = RunReplay(replay, play.files, jobs);
        ASSERT_TRUE(hash.has_value());
        EXPECT_EQ(*hash, replay.finalHash) << workers << " workers";
    }
}

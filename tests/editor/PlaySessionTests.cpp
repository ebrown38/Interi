// SPDX-License-Identifier: MIT
#include <interi/editor/PlaySession.h>
#include <interi/editor/Replay.h>
#include <interi/editor/StructureDebug.h>
#include <interi/jobs/JobSystem.h>

#include "PlayTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace interi;
using namespace interi::editor;
using interi::editor::testing::PlayLevel;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// Plays the level for `steps`, setting off a blast before step `blastAt` (none if past the end).
Replay Record(const PlayLevel& play, JobSystem& jobs, std::uint64_t steps, std::uint64_t blastAt)
{
    auto session = PlaySession::Start(play.level, play.files, jobs);
    EXPECT_TRUE(session.has_value()) << session.error().message;
    for (std::uint64_t step = 0; step < steps; ++step)
    {
        if (step == blastAt)
        {
            (*session)->Detonate(PlayLevel::Blast());
        }
        (*session)->Step();
    }
    return (*session)->Recording("levels/test.json");
}

} // namespace

TEST(StressOverlay, BondUtilization_ColorMapping)
{
    const auto near = [](const Vec3& a, const Vec3& b) { return Length(a - b) < 1e-5f; };
    EXPECT_TRUE(near(StressOverlayColor(0.0f), {0.1f, 0.85f, 0.2f})); // green: unloaded
    EXPECT_TRUE(near(StressOverlayColor(0.5f), {1.0f, 0.9f, 0.1f}));  // yellow: half way
    EXPECT_TRUE(near(StressOverlayColor(1.0f), {1.0f, 0.1f, 0.05f})); // red: about to break
    EXPECT_TRUE(near(StressOverlayColor(3.0f), StressOverlayColor(1.0f)));
    EXPECT_TRUE(near(StressOverlayColor(-1.0f), StressOverlayColor(0.0f)));
    EXPECT_TRUE(near(StressOverlayColor(std::numeric_limits<float>::quiet_NaN()), StressOverlayColor(0.0f)));
    // Monotonic: red rises to yellow, then green falls to red.
    EXPECT_LT(StressOverlayColor(0.2f).x, StressOverlayColor(0.4f).x);
    EXPECT_GT(StressOverlayColor(0.6f).y, StressOverlayColor(0.9f).y);
}

TEST(StressOverlay, PlaySession_LinesAndAnchors)
{
    JobSystem jobs(Workers(2));
    const PlayLevel play;
    auto session = PlaySession::Start(play.level, play.files, jobs);
    ASSERT_TRUE(session.has_value()) << session.error().message;
    for (int i = 0; i < 30; ++i)
    {
        (*session)->Step();
    }
    const auto& destructibles = (*session)->Level().destructibles;
    ASSERT_EQ(destructibles.size(), 2u);
    const auto lines = StressOverlayLines((*session)->GetWorld(), (*session)->Destruction(), destructibles[0]);
    ASSERT_EQ(lines.size(), 13u); // 2 x 5 grid: 5 across + 8 up
    float peak = 0.0f;
    for (const BondLine& line : lines)
    {
        EXPECT_NEAR(Length(line.to - line.from), 1.0f, 1e-3f); // neighbouring unit cubes
        peak = std::max(peak, line.utilization);
    }
    EXPECT_GT(peak, 0.0f); // the self-weight loads the lower bonds
    EXPECT_LT(peak, 1.0f);
    const auto anchors = AnchorPoints((*session)->GetWorld(), (*session)->Destruction(), destructibles[0]);
    ASSERT_EQ(anchors.size(), 2u);
    for (const Vec3& anchor : anchors)
    {
        EXPECT_NEAR(anchor.y, 0.5f, 1e-3f);
        EXPECT_LT(anchor.x, 0.0f); // the first tower stands at x = -3
    }
}

TEST(Replay, RecordedEvent_ReproducesSameHash)
{
    JobSystem jobs(Workers(4));
    const PlayLevel play;
    const Replay recorded = Record(play, jobs, 120, 10);
    ASSERT_EQ(recorded.events.size(), 1u);
    EXPECT_EQ(recorded.events[0].step, 10u);
    EXPECT_EQ(recorded.steps, 120u);

    // Through the file format and back, then played from a fresh spawn.
    const auto reread = ReadReplay(WriteReplay(recorded));
    ASSERT_TRUE(reread.has_value()) << reread.error().message;
    const auto hash = RunReplay(*reread, play.files, jobs);
    ASSERT_TRUE(hash.has_value()) << hash.error().message;
    EXPECT_EQ(*hash, recorded.finalHash);

    // The event matters: without it (or at another step) the end state differs.
    EXPECT_NE(Record(play, jobs, 120, 1000).finalHash, recorded.finalHash);
    EXPECT_NE(Record(play, jobs, 120, 11).finalHash, recorded.finalHash);
}

TEST(Replay, BlastBreaksBonds)
{
    JobSystem jobs(Workers(2));
    const PlayLevel play;
    auto session = PlaySession::Start(play.level, play.files, jobs);
    ASSERT_TRUE(session.has_value());
    const auto id = (*session)->Level().destructibles[0];
    const auto intact = StressOverlayLines((*session)->GetWorld(), (*session)->Destruction(), id).size();
    (*session)->Detonate(PlayLevel::Blast());
    for (int i = 0; i < 5; ++i)
    {
        (*session)->Step();
    }
    EXPECT_LT(StressOverlayLines((*session)->GetWorld(), (*session)->Destruction(), id).size(), intact);
}

TEST(Replay, File_RoundTripsAndDetectsCorruption)
{
    Replay replay;
    replay.levelPath = "levels/a.json";
    replay.levelJson = "{}";
    replay.steps = 77;
    replay.events.push_back({.step = 3, .damage = PlayLevel::Blast()});
    replay.finalHash = 0x1234567890ABCDEFull;
    std::vector<std::byte> bytes = WriteReplay(replay);
    const auto read = ReadReplay(bytes);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->levelPath, replay.levelPath);
    EXPECT_EQ(read->steps, 77u);
    ASSERT_EQ(read->events.size(), 1u);
    EXPECT_EQ(read->events[0].damage.energy, PlayLevel::Blast().energy);
    EXPECT_EQ(read->finalHash, replay.finalHash);

    bytes[bytes.size() - 3] ^= std::byte{0x40};
    const auto corrupt = ReadReplay(bytes);
    ASSERT_FALSE(corrupt.has_value());
    EXPECT_EQ(corrupt.error().code, ErrorCode::Corrupt);
    EXPECT_FALSE(ReadReplay(std::span<const std::byte>()).has_value());
}

TEST(Replay, MissingAsset_Fails)
{
    JobSystem jobs(Workers(1));
    const PlayLevel play;
    MemoryFileSystem empty;
    EXPECT_FALSE(PlaySession::Start(play.level, empty, jobs).has_value());
    Replay replay;
    replay.levelJson = "not json";
    EXPECT_FALSE(RunReplay(replay, play.files, jobs).has_value());
}

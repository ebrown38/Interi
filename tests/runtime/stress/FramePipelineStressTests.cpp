// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/runtime/FramePipeline.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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

} // namespace

// Simulation (parallel, on the jobs) writes frame N+1 while render prep (also parallel) reads frame N. Every value
// render prep reads must belong to one frame; under TSan any shared write would also be reported as a race.
TEST(FramePipeline, SimAndRender_NoSharedWriteRace)
{
    constexpr std::size_t kValues = 64 * 1024;
    for (const std::uint32_t workers : {1u, 4u, 15u})
    {
        JobSystemDesc desc;
        desc.workerCount = workers;
        JobSystem jobs(desc);
        FramePipeline<Snapshot> pipeline(jobs);
        std::uint64_t renderedFrames = 0;
        std::uint64_t mismatches = 0;
        for (std::uint64_t frame = 1; frame <= 200; ++frame)
        {
            pipeline.RunFrame(
                [&](Snapshot& out)
                {
                    out.frame = frame;
                    out.values.resize(kValues);
                    interi::ParallelFor(jobs, 0, kValues, 4096,
                                        [&out, frame](std::size_t i) { out.values[i] = frame * 1'000'003 + i; });
                },
                [&](const Snapshot& in)
                {
                    ++renderedFrames;
                    const std::uint64_t bad = interi::ParallelReduce(
                        jobs, 0, kValues, 4096, std::uint64_t{0}, [&in](std::size_t i) -> std::uint64_t
                        { return in.values[i] != in.frame * 1'000'003 + i ? 1 : 0; },
                        [](std::uint64_t a, std::uint64_t b) { return a + b; });
                    mismatches += bad;
                });
        }
        EXPECT_EQ(renderedFrames, 199u) << workers << " workers";
        EXPECT_EQ(mismatches, 0u) << workers << " workers";
    }
}

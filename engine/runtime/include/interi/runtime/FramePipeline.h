// SPDX-License-Identifier: MIT
#pragma once

#include <interi/jobs/JobSystem.h>

#include <array>
#include <cstddef>
#include <utility>

namespace interi
{

/// Overlaps simulation of frame N+1 with render preparation of frame N.
///
/// The pipeline holds two snapshots. Each RunFrame starts render prep of the most recently published snapshot as a
/// job, simulates into the other snapshot on the calling thread, waits for render prep, then publishes the new
/// snapshot. Simulation only writes its snapshot and render prep only reads the published one, so the two never
/// share written state. Either side may itself run parallel work on the same JobSystem.
template <typename Snapshot>
class FramePipeline
{
public:
    explicit FramePipeline(JobSystem& jobs) noexcept
        : m_jobs(&jobs)
    {
    }

    /// `simulate(Snapshot&)` fills the next snapshot; `renderPrep(const Snapshot&)` consumes the previous one (not
    /// called on the first frame). Call from a thread that may wait on the JobSystem, not from inside a job.
    template <typename Simulate, typename RenderPrep>
    void RunFrame(Simulate&& simulate, RenderPrep&& renderPrep)
    {
        JobCounter rendering;
        if (m_hasPublished)
        {
            const Snapshot* published = &m_snapshots[m_published];
            auto* prep = &renderPrep;
            m_jobs->Submit([prep, published] { (*prep)(*published); }, &rendering, JobPriority::High);
        }
        const std::size_t next = 1 - m_published;
        simulate(m_snapshots[next]);
        m_jobs->WaitFor(rendering);
        m_published = next;
        m_hasPublished = true;
    }

    /// The most recently simulated snapshot (valid after the first RunFrame).
    [[nodiscard]] const Snapshot& Latest() const noexcept { return m_snapshots[m_published]; }

private:
    JobSystem* m_jobs;
    std::array<Snapshot, 2> m_snapshots{};
    std::size_t m_published = 1;
    bool m_hasPublished = false;
};

} // namespace interi

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/jobs/JobSystem.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
// clang-format on

#include <cstdint>

namespace interi
{

/// Runs Jolt's jobs on an Interi JobSystem, so the physics step uses the engine's workers instead of a second
/// thread pool. Jolt jobs are queued as High priority Interi jobs; barrier waits are Jolt's own
/// (JobSystemWithBarrier), and a waiting thread also executes ready jobs from its barrier.
///
/// Like JPH::JobSystemThreadPool, jobs are not queued when the JobSystem has no workers: they run when a barrier
/// that contains them is waited on. Jolt's allocator must be registered (JPH::RegisterDefaultAllocator or a custom
/// one) before constructing this, because Jolt's job objects and this class use Jolt's operator new.
///
/// A queued job keeps its slot until the Interi job that runs it has finished, even if a barrier executed it first.
/// When all `maxJobs` slots are in use, CreateJob yields until one frees (as JPH::JobSystemThreadPool does).
///
/// The Interi JobSystem must outlive this object. The destructor waits until every queued job has been released.
class JoltJobSystem final : public JPH::JobSystemWithBarrier
{
public:
    static constexpr std::uint32_t kDefaultMaxJobs = 2048;  ///< JPH::cMaxPhysicsJobs
    static constexpr std::uint32_t kDefaultMaxBarriers = 8; ///< JPH::cMaxPhysicsBarriers

    // `interi::` throughout: inside this class, plain `JobSystem` names the JPH::JobSystem base.
    explicit JoltJobSystem(interi::JobSystem& jobs, std::uint32_t maxJobs = kDefaultMaxJobs,
                           std::uint32_t maxBarriers = kDefaultMaxBarriers);
    ~JoltJobSystem() override;

    JoltJobSystem(const JoltJobSystem&) = delete;
    JoltJobSystem& operator=(const JoltJobSystem&) = delete;
    JoltJobSystem(JoltJobSystem&&) = delete;
    JoltJobSystem& operator=(JoltJobSystem&&) = delete;

    /// Workers plus the thread that waits on the barrier.
    [[nodiscard]] int GetMaxConcurrency() const override;
    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 numDependencies = 0) override;

protected:
    void QueueJob(Job* job) override;
    void QueueJobs(Job** jobs, JPH::uint count) override;
    void FreeJob(Job* job) override;

private:
    interi::JobSystem& m_jobs;
    JPH::FixedSizeFreeList<Job> m_joltJobs;
    JobCounter m_outstanding; ///< Interi jobs that still hold a reference to a Jolt job.
};

} // namespace interi

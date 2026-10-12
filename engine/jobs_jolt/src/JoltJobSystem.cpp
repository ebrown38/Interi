// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/jobs_jolt/JoltJobSystem.h>

#include <thread>

namespace interi
{

JoltJobSystem::JoltJobSystem(interi::JobSystem& jobs, std::uint32_t maxJobs, std::uint32_t maxBarriers)
    : JobSystemWithBarrier(maxBarriers)
    , m_jobs(jobs)
{
    m_joltJobs.Init(maxJobs, maxJobs);
}

JoltJobSystem::~JoltJobSystem()
{
    m_jobs.WaitFor(m_outstanding);
}

int JoltJobSystem::GetMaxConcurrency() const
{
    return static_cast<int>(m_jobs.ThreadCount());
}

JPH::JobHandle JoltJobSystem::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                        JPH::uint32 numDependencies)
{
    JPH::uint32 index = m_joltJobs.ConstructObject(name, color, this, function, numDependencies);
    if (index == decltype(m_joltJobs)::cInvalidObjectIndex)
    {
        INTERI_ASSERT(false, "JoltJobSystem: out of Jolt jobs; raise maxJobs");
        do
        {
            std::this_thread::yield(); // running jobs release theirs eventually (same as JobSystemThreadPool)
            index = m_joltJobs.ConstructObject(name, color, this, function, numDependencies);
        } while (index == decltype(m_joltJobs)::cInvalidObjectIndex);
    }
    Job* job = &m_joltJobs.Get(index);
    JobHandle handle(job); // keeps a reference: the job may run and finish as soon as it is queued
    if (numDependencies == 0)
    {
        QueueJob(job);
    }
    return handle;
}

void JoltJobSystem::QueueJob(Job* job)
{
    if (m_jobs.WorkerCount() == 0)
    {
        return; // a barrier wait will execute it
    }
    job->AddRef(); // released once the Interi job has run it
    m_jobs.Submit(
        [job]
        {
            job->Execute(); // no-op if a barrier wait already ran it
            job->Release();
        },
        &m_outstanding, JobPriority::High);
}

void JoltJobSystem::QueueJobs(Job** jobs, JPH::uint count)
{
    for (JPH::uint i = 0; i < count; ++i)
    {
        QueueJob(jobs[i]);
    }
}

void JoltJobSystem::FreeJob(Job* job)
{
    m_joltJobs.DestructObject(job);
}

} // namespace interi

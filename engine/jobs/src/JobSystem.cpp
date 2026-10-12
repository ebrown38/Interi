// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Profiling.h>
#include <interi/jobs/JobSystem.h>

#include <algorithm>
#include <format>
#include <string>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <immintrin.h>
#endif

namespace interi
{

namespace
{

/// Per-thread scheduler state. A thread is a worker of at most one JobSystem.
struct ThreadState
{
    const JobSystem* system = nullptr;
    std::uint32_t workerIndex = 0;
    std::uint32_t waitDepth = 0;
    std::uint32_t random = 0x9E37'79B9; ///< xorshift state for picking steal victims
};

thread_local ThreadState t_thread;

/// Spins before a worker goes to sleep. Each spin is one pause instruction plus an atomic load.
constexpr int kSpinsBeforeSleep = 2000;
/// Failed WaitFor polls before the waiter starts yielding its time slice.
constexpr int kPausesBeforeYield = 64;

void CpuPause() noexcept
{
    _mm_pause();
}

std::uint32_t NextRandom(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void PinCurrentThread(std::uint32_t cpu) noexcept
{
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    const int result = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    INTERI_VERIFY(result == 0, "pthread_setaffinity_np failed");
#elif defined(_WIN32)
    const DWORD_PTR mask = DWORD_PTR{1} << (cpu % (sizeof(DWORD_PTR) * 8));
    const DWORD_PTR previous = SetThreadAffinityMask(GetCurrentThread(), mask);
    INTERI_VERIFY(previous != 0, "SetThreadAffinityMask failed");
#else
    (void)cpu;
#endif
}

} // namespace

JobSystem::Worker::Worker(std::size_t dequeCapacity)
    : deques{WorkStealingDeque<Job*>(dequeCapacity), WorkStealingDeque<Job*>(dequeCapacity),
             WorkStealingDeque<Job*>(dequeCapacity)}
{
}

std::uint32_t JobSystem::DefaultWorkerCount() noexcept
{
    return std::max(std::thread::hardware_concurrency(), 2u) - 1;
}

JobSystem::JobSystem(const JobSystemDesc& desc)
    : m_jobPool(sizeof(Job), std::max<std::uint32_t>(desc.maxJobs, 1), alignof(Job))
{
    for (auto& queue : m_injection)
    {
        queue = std::make_unique<MPMCQueue<Job*>>(m_jobPool.BlockCount()); // never full: bounded by the pool
    }
    const std::uint32_t workerCount =
        desc.workerCount == JobSystemDesc::kAutoWorkerCount ? DefaultWorkerCount() : desc.workerCount;
    m_workers.reserve(workerCount);
    for (std::uint32_t i = 0; i < workerCount; ++i)
    {
        m_workers.push_back(std::make_unique<Worker>(desc.dequeCapacity));
    }
    // Threads start only after every worker exists, since any worker may steal from any other.
    const std::uint32_t hardware = std::max(std::thread::hardware_concurrency(), 1u);
    m_threads.reserve(workerCount);
    for (std::uint32_t i = 0; i < workerCount; ++i)
    {
        m_threads.emplace_back(
            [this, i, pin = desc.pinThreads, hardware]
            {
                if (pin)
                {
                    PinCurrentThread((i + 1) % hardware);
                }
                WorkerMain(i);
            });
    }
}

JobSystem::~JobSystem()
{
    {
        const std::scoped_lock lock(m_sleepMutex);
        m_stopping.store(true, std::memory_order_seq_cst);
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads)
    {
        thread.join();
    }
    // Workers exit once they find no work. Run anything left (e.g. with zero workers) on this thread.
    while (Job* job = FindJob(std::nullopt, false, kJobPriorityCount))
    {
        Execute(job);
    }
}

std::optional<std::uint32_t> JobSystem::CurrentWorkerIndex() const noexcept
{
    if (t_thread.system == this)
    {
        return t_thread.workerIndex;
    }
    return std::nullopt;
}

JobSystem::Job* JobSystem::AllocateJob() noexcept
{
    return static_cast<Job*>(m_jobPool.Allocate(sizeof(Job), alignof(Job)));
}

void JobSystem::Enqueue(Job* job, JobPriority priority) noexcept
{
    const auto p = static_cast<std::size_t>(priority);
    INTERI_ASSERT(p < kJobPriorityCount, "invalid JobPriority");
    bool queued = false;
    if (t_thread.system == this)
    {
        queued = m_workers[t_thread.workerIndex]->deques[p].Push(job);
    }
    if (!queued)
    {
        INTERI_VERIFY(m_injection[p]->TryPush(job), "injection queue full (it is sized to the job pool)");
    }

    // Dekker with WorkerMain: either a sleeping worker sees m_queued > 0 in its wait predicate, or we see it in
    // m_sleepers and wake it. Both sides use seq_cst operations (no fences, so TSan can follow it).
    m_queued.fetch_add(1, std::memory_order_seq_cst);
    if (m_sleepers.load(std::memory_order_seq_cst) > 0)
    {
        const std::scoped_lock lock(m_sleepMutex);
        m_wake.notify_one();
    }
}

void JobSystem::Execute(Job* job) noexcept
{
    JobCounter* counter = job->counter;
    job->invoke(*job);
    m_jobPool.Free(job, sizeof(Job)); // before the counter drops, so a finished wait sees the job released
    if (counter != nullptr)
    {
        counter->m_pending.fetch_sub(1, std::memory_order_acq_rel);
    }
}

JobSystem::Job* JobSystem::FindJob(std::optional<std::uint32_t> workerIndex, bool ownDequeOnly,
                                   std::size_t priorityCount) noexcept
{
    const auto workerCount = static_cast<std::uint32_t>(m_workers.size());
    for (std::size_t p = 0; p < priorityCount; ++p)
    {
        Job* job = nullptr;
        if (workerIndex.has_value())
        {
            if (const std::optional<Job*> own = m_workers[*workerIndex]->deques[p].Pop())
            {
                job = *own;
            }
        }
        if (job == nullptr && !ownDequeOnly)
        {
            if (!m_injection[p]->TryPop(job))
            {
                job = nullptr;
                const std::uint32_t start = workerCount == 0 ? 0 : NextRandom(t_thread.random) % workerCount;
                for (std::uint32_t i = 0; i < workerCount && job == nullptr; ++i)
                {
                    const std::uint32_t victim = (start + i) % workerCount;
                    if (workerIndex.has_value() && victim == *workerIndex)
                    {
                        continue;
                    }
                    if (const std::optional<Job*> stolen = m_workers[victim]->deques[p].Steal())
                    {
                        job = *stolen;
                    }
                }
            }
        }
        if (job != nullptr)
        {
            m_queued.fetch_sub(1, std::memory_order_relaxed);
            return job;
        }
    }
    return nullptr;
}

void JobSystem::WaitFor(const JobCounter& counter)
{
    INTERI_ZONE();
    const std::optional<std::uint32_t> workerIndex = CurrentWorkerIndex();
    ++t_thread.waitDepth;
    // Past the nesting cap only take our own (usually descendant) jobs, to bound stack depth. Without workers the
    // caller is the only thread that can run anything, so it never restricts itself.
    const bool ownDequeOnly = t_thread.waitDepth > kMaxWaitNesting && !m_workers.empty();
    // Helpers skip Low (long background) jobs, unless there are no workers to run them.
    const std::size_t priorityCount = m_workers.empty() ? kJobPriorityCount : kJobPriorityCount - 1;
    int idlePolls = 0;
    while (!counter.IsDone())
    {
        if (Job* job = FindJob(workerIndex, ownDequeOnly, priorityCount))
        {
            Execute(job);
            idlePolls = 0;
        }
        else if (++idlePolls < kPausesBeforeYield)
        {
            CpuPause();
        }
        else
        {
            std::this_thread::yield();
        }
    }
    --t_thread.waitDepth;
}

void JobSystem::WorkerMain(std::uint32_t index)
{
    t_thread.system = this;
    t_thread.workerIndex = index;
    t_thread.random = 0x9E37'79B9u ^ ((index + 1) * 0x85EB'CA6Bu);
#if INTERI_PROFILING_ACTIVE
    const std::string name = std::format("Interi Worker {}", index);
    INTERI_PROFILE_THREAD_NAME(name.c_str());
#endif

    for (;;)
    {
        if (Job* job = FindJob(index, false, kJobPriorityCount))
        {
            Execute(job);
            continue;
        }
        bool workAppeared = false;
        for (int spin = 0; spin < kSpinsBeforeSleep && !workAppeared; ++spin)
        {
            CpuPause();
            workAppeared = m_queued.load(std::memory_order_relaxed) > 0;
        }
        if (workAppeared)
        {
            continue;
        }

        std::unique_lock lock(m_sleepMutex);
        m_sleepers.fetch_add(1, std::memory_order_seq_cst);
        m_wake.wait(
            lock, [this]
            { return m_queued.load(std::memory_order_seq_cst) > 0 || m_stopping.load(std::memory_order_seq_cst); });
        m_sleepers.fetch_sub(1, std::memory_order_seq_cst);
        if (m_queued.load(std::memory_order_seq_cst) <= 0 && m_stopping.load(std::memory_order_seq_cst))
        {
            break;
        }
    }
    t_thread.system = nullptr;
}

} // namespace interi

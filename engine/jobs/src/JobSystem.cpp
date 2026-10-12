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

/// Per-thread scheduler state. A thread owns a deque in at most one JobSystem: as one of its workers, or as the
/// thread that constructed it. Systems are identified by a unique id rather than their address, so a destroyed
/// system's address being reused can never alias.
struct ThreadState
{
    std::uint64_t systemId = 0; ///< 0: owns no deque
    std::uint32_t queueIndex = 0;
    bool isWorker = false;
    std::uint32_t waitDepth = 0;
    std::uint32_t random = 0x9E37'79B9; ///< xorshift state for picking steal victims
};

thread_local ThreadState t_thread;

std::atomic<std::uint64_t> g_nextSystemId{1};

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

JobSystem::ThreadQueues::ThreadQueues(std::size_t dequeCapacity)
    : deques{WorkStealingDeque<Job*>(dequeCapacity), WorkStealingDeque<Job*>(dequeCapacity),
             WorkStealingDeque<Job*>(dequeCapacity)}
{
}

std::uint32_t JobSystem::DefaultWorkerCount() noexcept
{
    return std::max(std::thread::hardware_concurrency(), 2u) - 1;
}

JobSystem::JobSystem(const JobSystemDesc& desc)
    : m_id(g_nextSystemId.fetch_add(1, std::memory_order_relaxed))
    , m_jobPool(sizeof(Job), std::max<std::uint32_t>(desc.maxJobs, 1), alignof(Job))
{
    for (auto& queue : m_injection)
    {
        queue = std::make_unique<MPMCQueue<Job*>>(m_jobPool.BlockCount()); // never full: bounded by the pool
    }
    const std::uint32_t workerCount =
        desc.workerCount == JobSystemDesc::kAutoWorkerCount ? DefaultWorkerCount() : desc.workerCount;
    // One deque set per worker plus one (the last) for the constructing thread. Its submissions then stay LIFO for
    // itself, so a nested WaitFor on that thread runs its own children first instead of unrelated older jobs.
    m_queues.reserve(workerCount + 1);
    for (std::uint32_t i = 0; i <= workerCount; ++i)
    {
        m_queues.push_back(std::make_unique<ThreadQueues>(desc.dequeCapacity));
    }
    if (t_thread.systemId == 0)
    {
        t_thread.systemId = m_id;
        t_thread.queueIndex = workerCount;
        t_thread.isWorker = false;
    }
    // Threads start only after every deque exists, since any worker may steal from any other.
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
    while (Job* job = FindJob(CurrentQueueIndex(), false, kJobPriorityCount))
    {
        Execute(job);
    }
    if (t_thread.systemId == m_id)
    {
        t_thread = ThreadState{};
    }
}

std::optional<std::uint32_t> JobSystem::CurrentWorkerIndex() const noexcept
{
    if (t_thread.systemId == m_id && t_thread.isWorker)
    {
        return t_thread.queueIndex;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> JobSystem::CurrentQueueIndex() const noexcept
{
    if (t_thread.systemId == m_id)
    {
        return t_thread.queueIndex;
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
    if (t_thread.systemId == m_id)
    {
        queued = m_queues[t_thread.queueIndex]->deques[p].Push(job);
    }
    if (!queued)
    {
        INTERI_VERIFY(m_injection[p]->TryPush(job), "injection queue full (it is sized to the job pool)");
    }

    // Wake a sleeper only when no worker is already searching: a searcher that finds this job wakes the next one
    // (WorkerMain), so bursts of submits cost one wake-up rather than one per job.
    m_queued.fetch_add(1, std::memory_order_seq_cst);
    if (m_searching.load(std::memory_order_seq_cst) == 0)
    {
        WakeOneSleeper();
    }
}

void JobSystem::WakeOneSleeper() noexcept
{
    // Dekker with WorkerMain's sleep path: a worker going to sleep increments m_sleepers and then re-checks
    // m_queued under the mutex, so either it sees the new job or we see it here. seq_cst operations rather than
    // fences, so ThreadSanitizer can follow the argument.
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

JobSystem::Job* JobSystem::FindJob(std::optional<std::uint32_t> queueIndex, bool ownDequeOnly,
                                   std::size_t priorityCount) noexcept
{
    const auto queueCount = static_cast<std::uint32_t>(m_queues.size());
    for (std::size_t p = 0; p < priorityCount; ++p)
    {
        Job* job = nullptr;
        if (queueIndex.has_value())
        {
            if (const std::optional<Job*> own = m_queues[*queueIndex]->deques[p].Pop())
            {
                job = *own;
            }
        }
        if (job == nullptr && !ownDequeOnly)
        {
            if (!m_injection[p]->TryPop(job))
            {
                job = nullptr;
                const std::uint32_t start = NextRandom(t_thread.random) % queueCount;
                for (std::uint32_t i = 0; i < queueCount && job == nullptr; ++i)
                {
                    const std::uint32_t victim = (start + i) % queueCount;
                    if (queueIndex.has_value() && victim == *queueIndex)
                    {
                        continue;
                    }
                    if (const std::optional<Job*> stolen = m_queues[victim]->deques[p].Steal())
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
    const std::optional<std::uint32_t> queueIndex = CurrentQueueIndex();
    ++t_thread.waitDepth;
    // Past the nesting cap only take our own (usually descendant) jobs, to bound stack depth. Without workers the
    // caller is the only thread that can run anything, so it never restricts itself.
    const bool ownDequeOnly = t_thread.waitDepth > kMaxWaitNesting && !m_threads.empty();
    // Helpers skip Low (long background) jobs, unless there are no workers to run them.
    const std::size_t priorityCount = m_threads.empty() ? kJobPriorityCount : kJobPriorityCount - 1;
    int idlePolls = 0;
    while (!counter.IsDone())
    {
        if (Job* job = FindJob(queueIndex, ownDequeOnly, priorityCount))
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
    t_thread.systemId = m_id;
    t_thread.queueIndex = index;
    t_thread.isWorker = true;
    t_thread.random = 0x9E37'79B9u ^ ((index + 1) * 0x85EB'CA6Bu);
#if INTERI_PROFILING_ACTIVE
    const std::string name = std::format("Interi Worker {}", index);
    INTERI_PROFILE_THREAD_NAME(name.c_str());
#endif

    for (;;)
    {
        Job* job = FindJob(index, false, kJobPriorityCount);
        if (job == nullptr)
        {
            // Search: spin for a while before sleeping, since new work usually arrives soon.
            m_searching.fetch_add(1, std::memory_order_seq_cst);
            for (int spin = 0; spin < kSpinsBeforeSleep && job == nullptr; ++spin)
            {
                CpuPause();
                if (spin % 32 == 31 && m_queued.load(std::memory_order_relaxed) > 0)
                {
                    job = FindJob(index, false, kJobPriorityCount);
                }
            }
            const std::uint32_t searchers = m_searching.fetch_sub(1, std::memory_order_seq_cst);
            if (job != nullptr && searchers == 1 && m_queued.load(std::memory_order_seq_cst) > 0)
            {
                WakeOneSleeper(); // the last searcher found work and more is queued: hand the search on
            }
        }
        if (job != nullptr)
        {
            Execute(job);
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
    t_thread = ThreadState{};
}

} // namespace interi

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/containers/MPMCQueue.h>
#include <interi/core/memory/PoolAllocator.h>
#include <interi/jobs/WorkStealingDeque.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace interi
{

/// Scheduling class of a job. Queued jobs of a higher priority always start before queued jobs of a lower one.
enum class JobPriority : std::uint8_t
{
    High,   ///< Physics/destruction critical path.
    Normal, ///< Default.
    Low     ///< Background work (IO, cooking). Never picked up by a thread that is helping inside WaitFor.
};

inline constexpr std::size_t kJobPriorityCount = 3;

/// Counts unfinished jobs. Submit(..., &counter) increments it; it drops when each job finishes. A counter must
/// outlive the jobs that reference it, and is reusable once it reaches zero.
class JobCounter
{
public:
    JobCounter() = default;
    JobCounter(const JobCounter&) = delete;
    JobCounter& operator=(const JobCounter&) = delete;
    JobCounter(JobCounter&&) = delete;
    JobCounter& operator=(JobCounter&&) = delete;
    ~JobCounter() = default;

    /// True when every job submitted against this counter has finished (acquire: their writes are visible).
    [[nodiscard]] bool IsDone() const noexcept { return m_pending.load(std::memory_order_acquire) == 0; }
    [[nodiscard]] std::int64_t Pending() const noexcept { return m_pending.load(std::memory_order_acquire); }

private:
    friend class JobSystem;
    std::atomic<std::int64_t> m_pending{0};
};

struct JobSystemDesc
{
    static constexpr std::uint32_t kAutoWorkerCount = 0xFFFF'FFFF;

    /// Worker threads to start. kAutoWorkerCount = JobSystem::DefaultWorkerCount(). 0 is allowed: jobs then run
    /// only on threads that call WaitFor (or in the destructor).
    std::uint32_t workerCount = kAutoWorkerCount;
    /// Pin worker i to logical CPU i + 1 (CPU 0 is left to the main thread).
    bool pinThreads = false;
    /// Jobs that can be queued or running at once. When the pool is full, Submit runs the job inline.
    std::uint32_t maxJobs = 16 * 1024;
    /// Capacity of each worker's per-priority deque. Overflow goes to the shared injection queue.
    std::uint32_t dequeCapacity = 1024;
};

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

/// Work-stealing job scheduler. Each worker owns one Chase-Lev deque per priority; threads that are not workers
/// submit through a shared MPMC injection queue per priority. A worker looks for work in priority order, and
/// within a priority checks its own deque, then the injection queue, then steals from other workers.
///
/// Jobs are callables with up to kJobStorageSize bytes of captures, stored inline (Submit never allocates from
/// the heap). Jobs must not throw: an escaping exception terminates the process.
///
/// WaitFor never blocks idle: the waiting thread runs other High/Normal jobs until the counter reaches zero.
/// Nested waits are supported up to kMaxWaitNesting levels per thread; past that a waiter only runs jobs from its
/// own deque, so dependency chains deeper than ThreadCount() * kMaxWaitNesting waits are unsupported.
///
/// The destructor drains: every job submitted before or during destruction runs before it returns. Submitting
/// from a non-worker thread once destruction has started is not allowed.
class JobSystem
{
public:
    static constexpr std::size_t kJobStorageSize = 48;
    static constexpr std::uint32_t kMaxWaitNesting = 32;

    explicit JobSystem(const JobSystemDesc& desc = {});
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// hardware_concurrency() - 1 (the main thread is the remaining one), at least 1.
    [[nodiscard]] static std::uint32_t DefaultWorkerCount() noexcept;

    /// Queues `function` to run once on some thread. If `counter` is set it is incremented now and decremented
    /// after the job finishes. Thread-safe; may be called from inside jobs.
    template <typename F>
    void Submit(F&& function, JobCounter* counter = nullptr, JobPriority priority = JobPriority::Normal);

    /// Returns once `counter` reaches zero, running other jobs on the calling thread meanwhile. Any thread.
    void WaitFor(const JobCounter& counter);

    [[nodiscard]] std::uint32_t WorkerCount() const noexcept { return static_cast<std::uint32_t>(m_workers.size()); }
    /// Threads that can run jobs at once: the workers plus one waiting thread.
    [[nodiscard]] std::uint32_t ThreadCount() const noexcept { return WorkerCount() + 1; }
    /// Index in [0, WorkerCount()) when called on one of this system's worker threads, otherwise nothing.
    [[nodiscard]] std::optional<std::uint32_t> CurrentWorkerIndex() const noexcept;

private:
    struct alignas(kCacheLineSize) Job
    {
        void (*invoke)(Job& job) noexcept; ///< Runs the stored callable, then destroys it.
        JobCounter* counter;
        alignas(16) std::byte storage[kJobStorageSize];
    };

    struct Worker
    {
        explicit Worker(std::size_t dequeCapacity);
        std::array<WorkStealingDeque<Job*>, kJobPriorityCount> deques;
    };

    [[nodiscard]] Job* AllocateJob() noexcept;
    void Enqueue(Job* job, JobPriority priority) noexcept;
    void Execute(Job* job) noexcept;
    /// Next job for the calling thread, or nullptr. `ownDequeOnly` restricts a worker to its own deques.
    [[nodiscard]] Job* FindJob(std::optional<std::uint32_t> workerIndex, bool ownDequeOnly,
                               std::size_t priorityCount) noexcept;
    void WorkerMain(std::uint32_t index);

    PoolAllocator m_jobPool;
    std::array<std::unique_ptr<MPMCQueue<Job*>>, kJobPriorityCount> m_injection;
    std::vector<std::unique_ptr<Worker>> m_workers;
    std::vector<std::thread> m_threads;

    alignas(kCacheLineSize) std::atomic<std::int64_t> m_queued{0}; ///< Jobs queued and not yet taken.
    alignas(kCacheLineSize) std::atomic<std::uint32_t> m_sleepers{0};
    std::atomic<bool> m_stopping{false};
    std::mutex m_sleepMutex;
    std::condition_variable m_wake;
};

INTERI_MSVC_WARNING_POP()

template <typename F>
void JobSystem::Submit(F&& function, JobCounter* counter, JobPriority priority)
{
    using Function = std::decay_t<F>;
    static_assert(sizeof(Function) <= kJobStorageSize, "job captures exceed JobSystem::kJobStorageSize bytes");
    static_assert(alignof(Function) <= 16, "job captures are over-aligned");
    static_assert(std::is_invocable_v<Function&>, "jobs take no arguments");

    Job* job = AllocateJob();
    if (job == nullptr)
    {
        function(); // pool exhausted: run inline rather than fail or block
        return;
    }
    try
    {
        ::new (static_cast<void*>(job->storage)) Function(std::forward<F>(function));
    }
    catch (...)
    {
        m_jobPool.Free(job, sizeof(Job));
        throw;
    }
    job->invoke = [](Job& self) noexcept
    {
        Function* stored = std::launder(reinterpret_cast<Function*>(self.storage));
        (*stored)();
        std::destroy_at(stored);
    };
    job->counter = counter;
    if (counter != nullptr)
    {
        counter->m_pending.fetch_add(1, std::memory_order_relaxed);
    }
    Enqueue(job, priority);
}

} // namespace interi

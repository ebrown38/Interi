// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/jobs/JobSystem.h>
#include <interi/rhi/CommandListPool.h>

#include <algorithm>
#include <utility>

namespace interi::rhi
{

CommandListPool::CommandListPool(IDevice& device, const JobSystem& jobs, std::uint32_t framesInFlight)
    : m_device(&device)
    , m_jobs(&jobs)
    , m_owner(std::this_thread::get_id())
    , m_frames(framesInFlight)
{
}

Result<CommandListPool> CommandListPool::Create(IDevice& device, const JobSystem& jobs, std::uint32_t framesInFlight)
{
    INTERI_ASSERT(framesInFlight > 0);
    CommandListPool pool(device, jobs, framesInFlight);
    for (Frame& frame : pool.m_frames)
    {
        frame.pools.reserve(jobs.ThreadCount());
        for (std::uint32_t i = 0; i < jobs.ThreadCount(); ++i)
        {
            INTERI_TRY_ASSIGN(handle, device.CreateCommandPool());
            frame.pools.push_back(handle);
        }
    }
    return pool;
}

CommandListPool::CommandListPool(CommandListPool&& other) noexcept
    : m_device(std::exchange(other.m_device, nullptr))
    , m_jobs(other.m_jobs)
    , m_owner(other.m_owner)
    , m_frames(std::move(other.m_frames))
    , m_current(other.m_current)
    , m_started(other.m_started)
{
}

CommandListPool::~CommandListPool()
{
    if (m_device == nullptr)
    {
        return;
    }
    std::uint64_t newest = 0;
    for (const Frame& frame : m_frames)
    {
        newest = std::max(newest, frame.submittedValue);
    }
    if (newest != 0)
    {
        m_device->WaitForValue(newest);
    }
    for (const Frame& frame : m_frames)
    {
        for (const CommandPoolHandle pool : frame.pools)
        {
            m_device->DestroyCommandPool(pool);
        }
    }
}

void CommandListPool::BeginFrame()
{
    INTERI_ASSERT(std::this_thread::get_id() == m_owner, "BeginFrame belongs to the thread that created the pool");
    if (m_started)
    {
        m_current = (m_current + 1) % static_cast<std::uint32_t>(m_frames.size());
    }
    m_started = true;

    Frame& frame = m_frames[m_current];
    if (frame.submittedValue != 0)
    {
        m_device->WaitForValue(frame.submittedValue);
    }
    for (const CommandPoolHandle pool : frame.pools)
    {
        m_device->ResetCommandPool(pool);
    }
}

ICommandList& CommandListPool::Acquire()
{
    std::uint32_t slot = m_jobs->WorkerCount(); // the owning thread's pool
    if (const auto worker = m_jobs->CurrentWorkerIndex())
    {
        slot = *worker;
    }
    else
    {
        INTERI_ASSERT(std::this_thread::get_id() == m_owner,
                      "Acquire is only allowed on the owning thread and the job system's workers");
    }
    return m_device->BeginCommandList(m_frames[m_current].pools[slot]);
}

void CommandListPool::EndFrame(std::uint64_t submittedValue)
{
    INTERI_ASSERT(std::this_thread::get_id() == m_owner, "EndFrame belongs to the thread that created the pool");
    m_frames[m_current].submittedValue = submittedValue;
}

} // namespace interi::rhi

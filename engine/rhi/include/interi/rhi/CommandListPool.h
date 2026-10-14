// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Device.h>

#include <cstdint>
#include <thread>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::rhi
{

/// Command pools for multithreaded recording: one per thread per frame in flight, so threads never share a pool
/// and a frame's pools are recycled only after the GPU has finished that frame.
///
/// The thread that creates the pool owns it (BeginFrame/EndFrame/destruction). Acquire may be called from the
/// owning thread and from the worker threads of `jobs`.
class CommandListPool
{
public:
    [[nodiscard]] static Result<CommandListPool> Create(IDevice& device, const JobSystem& jobs,
                                                        std::uint32_t framesInFlight);
    /// Waits for every submitted frame, then destroys the pools.
    ~CommandListPool();

    CommandListPool(CommandListPool&& other) noexcept;
    CommandListPool& operator=(CommandListPool&&) = delete;
    CommandListPool(const CommandListPool&) = delete;
    CommandListPool& operator=(const CommandListPool&) = delete;

    /// Moves to the next frame slot: waits until the GPU finished the frame that last used it, then resets its
    /// pools. Owning thread only.
    void BeginFrame();
    /// Begins a command list from the calling thread's pool for the current frame.
    [[nodiscard]] ICommandList& Acquire();
    /// Records the timeline value that the current frame's submission signals. Owning thread only.
    void EndFrame(std::uint64_t submittedValue);

private:
    CommandListPool(IDevice& device, const JobSystem& jobs, std::uint32_t framesInFlight);

    struct Frame
    {
        std::vector<CommandPoolHandle> pools; ///< Index = worker index; the last one belongs to the owning thread.
        std::uint64_t submittedValue = 0;
    };

    IDevice* m_device;
    const JobSystem* m_jobs;
    std::thread::id m_owner;
    std::vector<Frame> m_frames;
    std::uint32_t m_current = 0;
    bool m_started = false;
};

} // namespace interi::rhi

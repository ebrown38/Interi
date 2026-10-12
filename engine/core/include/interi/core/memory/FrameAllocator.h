// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/memory/LinearAllocator.h>

#include <cstddef>
#include <vector>

namespace interi
{

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

/// Per-thread, double-buffered frame arenas. Each thread allocates from its own LinearAllocator without locks;
/// memory allocated in frame N stays valid during frame N+1 (so it can be consumed one frame late, e.g. by
/// render submission) and is reclaimed when frame N+2 begins.
class FrameAllocators
{
public:
    /// Creates 2 * threadCount arenas of `bytesPerThread` each.
    FrameAllocators(std::size_t threadCount, std::size_t bytesPerThread, IAllocator& backing = GetDefaultAllocator());

    /// The current frame's arena for a thread index in [0, ThreadCount()). Only that thread may use it.
    [[nodiscard]] LinearAllocator& ForThread(std::size_t threadIndex) noexcept;

    /// Flips to the other buffer and resets it. Call once per frame while no thread is allocating.
    void BeginFrame() noexcept;

    [[nodiscard]] std::size_t ThreadCount() const noexcept { return m_threadCount; }

private:
    /// Cache-line aligned so arenas used by different threads never share a line.
    struct alignas(kCacheLineSize) Arena
    {
        LinearAllocator allocator;
    };

    std::size_t m_threadCount;
    std::size_t m_current = 0;
    std::vector<Arena> m_arenas; ///< [buffer * threadCount + thread]
};

INTERI_MSVC_WARNING_POP()

} // namespace interi

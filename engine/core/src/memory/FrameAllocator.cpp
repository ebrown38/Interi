// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/memory/FrameAllocator.h>

namespace interi
{

FrameAllocators::FrameAllocators(std::size_t threadCount, std::size_t bytesPerThread, IAllocator& backing)
    : m_threadCount(threadCount)
{
    m_arenas.reserve(threadCount * 2);
    for (std::size_t i = 0; i < threadCount * 2; ++i)
    {
        m_arenas.push_back(Arena{LinearAllocator(bytesPerThread, backing)});
    }
}

LinearAllocator& FrameAllocators::ForThread(std::size_t threadIndex) noexcept
{
    INTERI_ASSERT(threadIndex < m_threadCount, "thread index out of range");
    return m_arenas[m_current * m_threadCount + threadIndex].allocator;
}

void FrameAllocators::BeginFrame() noexcept
{
    m_current ^= 1;
    for (std::size_t t = 0; t < m_threadCount; ++t)
    {
        m_arenas[m_current * m_threadCount + t].allocator.Reset();
    }
}

} // namespace interi

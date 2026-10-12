// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Config.h>
#include <interi/core/memory/LinearAllocator.h>

#include <bit>
#include <cstdint>
#include <new>
#include <utility>

namespace interi
{

LinearAllocator::LinearAllocator(std::size_t capacity, IAllocator& backing)
    : m_backing(&backing)
    , m_begin(static_cast<std::byte*>(backing.Allocate(capacity, kCacheLineSize)))
    , m_capacity(capacity)
{
    if (m_begin == nullptr)
    {
        throw std::bad_alloc();
    }
}

LinearAllocator::~LinearAllocator()
{
    if (m_begin != nullptr)
    {
        m_backing->Free(m_begin, m_capacity);
    }
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : m_backing(other.m_backing)
    , m_begin(std::exchange(other.m_begin, nullptr))
    , m_capacity(std::exchange(other.m_capacity, 0))
    , m_offset(std::exchange(other.m_offset, 0))
{
}

void* LinearAllocator::Allocate(std::size_t size, std::size_t alignment) noexcept
{
    INTERI_ASSERT(std::has_single_bit(alignment), "alignment must be a power of two");
    // Align the address, not the offset, so alignments larger than the block's own alignment work.
    const auto base = reinterpret_cast<std::uintptr_t>(m_begin);
    const std::size_t aligned = AlignUp(base + m_offset, alignment) - base;
    if (aligned > m_capacity || size > m_capacity - aligned)
    {
        return nullptr;
    }
    m_offset = aligned + size;
    return m_begin + aligned;
}

void LinearAllocator::Rewind(Marker marker) noexcept
{
    INTERI_ASSERT(marker <= m_offset, "marker is newer than the current position");
    m_offset = marker;
}

} // namespace interi

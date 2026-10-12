// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <cstddef>

namespace interi
{

/// Bump allocator over one fixed block. Free() is a no-op; memory is reclaimed all at once with Reset() or
/// back to a Marker with Rewind(). Not thread-safe: use one per thread (see FrameAllocators).
class LinearAllocator final : public IAllocator
{
public:
    /// Opaque position for Rewind().
    using Marker = std::size_t;

    /// Reserves `capacity` bytes from `backing` (aligned to a cache line).
    explicit LinearAllocator(std::size_t capacity, IAllocator& backing = GetDefaultAllocator());
    ~LinearAllocator() override;

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    LinearAllocator(LinearAllocator&& other) noexcept;
    LinearAllocator& operator=(LinearAllocator&&) = delete;

    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    void Free(void* /*ptr*/, std::size_t /*size*/) noexcept override {}

    /// Releases every allocation.
    void Reset() noexcept { m_offset = 0; }
    [[nodiscard]] Marker GetMarker() const noexcept { return m_offset; }
    /// Releases everything allocated after `marker` was taken.
    void Rewind(Marker marker) noexcept;

    /// Bytes consumed, including alignment padding.
    [[nodiscard]] std::size_t Used() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

private:
    IAllocator* m_backing;
    std::byte* m_begin;
    std::size_t m_capacity;
    std::size_t m_offset = 0;
};

} // namespace interi

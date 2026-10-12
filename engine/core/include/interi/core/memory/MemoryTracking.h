// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace interi
{

/// Budget category an allocation is charged to.
enum class MemoryTag : std::uint8_t
{
    General,
    Core,
    Jobs,
    Physics,
    Destruction,
    Render,
    Audio,
    Assets,
    Gameplay,
    Editor,
    Count
};

[[nodiscard]] std::string_view ToString(MemoryTag tag) noexcept;

/// Snapshot of one tag's counters.
struct MemoryTagStats
{
    std::uint64_t currentBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t allocationCount = 0; ///< Lifetime total.
    std::uint64_t freeCount = 0;       ///< Lifetime total.
};

/// Process-wide counters per tag. All functions are thread-safe (relaxed atomics; peak is a CAS high-water mark).
[[nodiscard]] MemoryTagStats GetMemoryStats(MemoryTag tag) noexcept;
void RecordAllocation(MemoryTag tag, std::size_t bytes) noexcept;
void RecordFree(MemoryTag tag, std::size_t bytes) noexcept;

/// Forwards to another allocator and charges successful allocations to a tag. Counts requested bytes (not
/// padding). Thread-safe if the inner allocator is.
class TrackedAllocator final : public IAllocator
{
public:
    TrackedAllocator(IAllocator& inner, MemoryTag tag) noexcept
        : m_inner(&inner)
        , m_tag(tag)
    {
    }

    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    void Free(void* ptr, std::size_t size) noexcept override;

    [[nodiscard]] MemoryTag Tag() const noexcept { return m_tag; }

private:
    IAllocator* m_inner;
    MemoryTag m_tag;
};

} // namespace interi

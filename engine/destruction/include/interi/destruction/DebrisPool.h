// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <cstdint>
#include <optional>

namespace interi::destruction
{

/// A slot of a DebrisPool. Stale handles (released or evicted) are detected.
struct DebrisSlot
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0; ///< 0 = invalid.

    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(const DebrisSlot&, const DebrisSlot&) = default;
};

/// A fixed number of debris slots with a hard cap and priority eviction. Each slot holds a user value (e.g. a packed
/// entity) and a priority (higher = more worth keeping). Storage is allocated once at construction: acquiring,
/// evicting and releasing never allocate.
///
/// When full, Acquire evicts the live slot with the lowest priority (ties: lowest slot index), unless the newcomer's
/// priority is lower still, in which case it is rejected. Eviction scans the slots (O(capacity)).
///
/// Not thread-safe, except that different slots' priorities may be set concurrently (SetPriority / Priorities()).
class DebrisPool
{
public:
    struct Acquired
    {
        DebrisSlot slot;                      ///< Invalid if rejected.
        std::optional<std::uint64_t> evicted; ///< The user value of the slot evicted to make room.
    };

    explicit DebrisPool(std::uint32_t capacity, IAllocator& allocator = GetDefaultAllocator());
    ~DebrisPool();

    DebrisPool(const DebrisPool&) = delete;
    DebrisPool& operator=(const DebrisPool&) = delete;
    DebrisPool(DebrisPool&&) = delete;
    DebrisPool& operator=(DebrisPool&&) = delete;

    [[nodiscard]] Acquired Acquire(std::uint64_t user, float priority) noexcept;

    /// Frees the slot. Returns false for a stale handle.
    bool Release(DebrisSlot slot) noexcept;

    [[nodiscard]] bool IsLive(DebrisSlot slot) const noexcept;
    [[nodiscard]] std::uint64_t User(DebrisSlot slot) const noexcept;
    [[nodiscard]] float Priority(DebrisSlot slot) const noexcept;
    void SetPriority(DebrisSlot slot, float priority) noexcept;

    /// The live slot at `index`, or an invalid handle if it is free.
    [[nodiscard]] DebrisSlot SlotAt(std::uint32_t index) const noexcept;

    /// Calls `function(DebrisSlot)` for every live slot in index order.
    template <typename F>
    void ForEachLive(F&& function) const
    {
        for (std::uint32_t index = 0; index < m_capacity; ++index)
        {
            if (m_live[index] != 0)
            {
                function(DebrisSlot{index, m_generations[index]});
            }
        }
    }

    [[nodiscard]] std::uint32_t Capacity() const noexcept { return m_capacity; }
    [[nodiscard]] std::uint32_t LiveCount() const noexcept { return m_liveCount; }
    [[nodiscard]] bool IsFull() const noexcept { return m_liveCount == m_capacity; }

private:
    [[nodiscard]] std::uint32_t LowestPriorityLive() const noexcept;

    IAllocator* m_allocator;
    std::uint32_t m_capacity;
    std::uint32_t m_liveCount = 0;
    std::uint32_t m_freeCount = 0;
    float* m_priorities = nullptr;
    std::uint64_t* m_users = nullptr;
    std::uint32_t* m_generations = nullptr;
    std::uint32_t* m_free = nullptr; ///< Stack of free slot indices; the lowest index is on top initially.
    std::uint8_t* m_live = nullptr;
};

} // namespace interi::destruction

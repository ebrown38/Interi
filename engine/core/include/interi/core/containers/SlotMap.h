// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/Hash.h>
#include <interi/core/memory/StlAllocator.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace interi
{

/// Generational reference to an element of a SlotMap<T>. Stays safe to hold after the element is removed:
/// lookups with a stale handle fail instead of returning whatever reused the slot. Generation 0 is never issued,
/// so a default-constructed handle is always invalid.
template <typename T>
class Handle
{
public:
    constexpr Handle() noexcept = default;

    [[nodiscard]] static constexpr Handle FromParts(std::uint32_t index, std::uint32_t generation) noexcept
    {
        Handle handle;
        handle.m_index = index;
        handle.m_generation = generation;
        return handle;
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept { return m_index; }
    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept { return m_generation; }
    /// True if this handle was ever issued (it may still be stale).
    [[nodiscard]] constexpr bool IsValid() const noexcept { return m_generation != 0; }
    /// Both halves in one integer, for hashing and serialization.
    [[nodiscard]] constexpr std::uint64_t Packed() const noexcept
    {
        return (static_cast<std::uint64_t>(m_generation) << 32) | m_index;
    }

    friend constexpr bool operator==(Handle, Handle) noexcept = default;
    friend constexpr auto operator<=>(Handle, Handle) noexcept = default;

private:
    std::uint32_t m_index = 0;
    std::uint32_t m_generation = 0;
};

template <typename T>
struct Hasher<Handle<T>>
{
    [[nodiscard]] constexpr std::uint64_t operator()(Handle<T> handle) const noexcept
    {
        return MixBits(handle.Packed());
    }
};

/// Dense storage with stable generational handles (entities, chunks).
///
/// Values live contiguously (iteration is a linear scan); removal swaps the last value into the hole, so value
/// addresses and dense order are not stable, but handles are. Insert, Remove and Get are O(1).
///
/// When a slot's generation reaches MaxGeneration it is retired rather than wrapped, so a stale handle can never
/// alias a later occupant. With the default 32-bit range that costs one slot per ~4 billion reuses.
template <typename T, std::uint32_t MaxGeneration = 0xFFFF'FFFFu>
class SlotMap
{
    static_assert(MaxGeneration >= 1, "MaxGeneration must allow at least one generation");

public:
    using HandleType = Handle<T>;
    using iterator = std::vector<T, StlAllocator<T>>::iterator;
    using const_iterator = std::vector<T, StlAllocator<T>>::const_iterator;

    explicit SlotMap(IAllocator& allocator = GetDefaultAllocator())
        : m_values(StlAllocator<T>(allocator))
        , m_denseToSlot(StlAllocator<std::uint32_t>(allocator))
        , m_slots(StlAllocator<Slot>(allocator))
    {
    }

    HandleType Insert(const T& value) { return Emplace(value); }
    HandleType Insert(T&& value) { return Emplace(std::move(value)); }

    template <typename... Args>
    HandleType Emplace(Args&&... args)
    {
        std::uint32_t slotIndex = m_freeHead;
        if (slotIndex == kNone)
        {
            INTERI_ASSERT(m_slots.size() < kNone, "SlotMap is full");
            slotIndex = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(Slot{.generation = 1, .dense = kNone, .nextFree = kNone});
        }
        m_values.emplace_back(std::forward<Args>(args)...); // may throw: leaves the free list untouched
        m_denseToSlot.push_back(slotIndex);
        Slot& slot = m_slots[slotIndex];
        if (slotIndex == m_freeHead)
        {
            m_freeHead = slot.nextFree;
        }
        slot.dense = static_cast<std::uint32_t>(m_values.size() - 1);
        slot.nextFree = kNone;
        return HandleType::FromParts(slotIndex, slot.generation);
    }

    /// Removes the element. Returns false (and does nothing) for stale or foreign handles.
    bool Remove(HandleType handle)
    {
        if (!Contains(handle))
        {
            return false;
        }
        Slot& slot = m_slots[handle.Index()];
        const std::uint32_t dense = slot.dense;
        const auto last = static_cast<std::uint32_t>(m_values.size() - 1);
        if (dense != last)
        {
            m_values[dense] = std::move(m_values[last]);
            m_denseToSlot[dense] = m_denseToSlot[last];
            m_slots[m_denseToSlot[dense]].dense = dense;
        }
        m_values.pop_back();
        m_denseToSlot.pop_back();
        Release(handle.Index());
        return true;
    }

    [[nodiscard]] bool Contains(HandleType handle) const noexcept
    {
        return handle.Index() < m_slots.size() && m_slots[handle.Index()].generation == handle.Generation() &&
               m_slots[handle.Index()].dense != kNone;
    }

    /// The element, or nullptr for stale or foreign handles. The pointer is invalidated by Insert and Remove.
    [[nodiscard]] T* Get(HandleType handle) noexcept
    {
        return Contains(handle) ? &m_values[m_slots[handle.Index()].dense] : nullptr;
    }
    [[nodiscard]] const T* Get(HandleType handle) const noexcept
    {
        return Contains(handle) ? &m_values[m_slots[handle.Index()].dense] : nullptr;
    }

    /// Handle of the element at a dense position in [0, Size()), e.g. while iterating.
    [[nodiscard]] HandleType HandleAt(std::size_t denseIndex) const noexcept
    {
        INTERI_ASSERT(denseIndex < m_values.size());
        const std::uint32_t slotIndex = m_denseToSlot[denseIndex];
        return HandleType::FromParts(slotIndex, m_slots[slotIndex].generation);
    }

    /// Removes everything; every outstanding handle becomes stale.
    void Clear()
    {
        for (const std::uint32_t slotIndex : m_denseToSlot)
        {
            Release(slotIndex);
        }
        m_values.clear();
        m_denseToSlot.clear();
    }

    [[nodiscard]] std::size_t Size() const noexcept { return m_values.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_values.empty(); }
    /// Slots permanently retired after exhausting their generations.
    [[nodiscard]] std::size_t RetiredSlotCount() const noexcept { return m_retired; }

    [[nodiscard]] iterator begin() noexcept { return m_values.begin(); }
    [[nodiscard]] iterator end() noexcept { return m_values.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return m_values.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return m_values.end(); }

private:
    static constexpr std::uint32_t kNone = 0xFFFF'FFFFu;

    struct Slot
    {
        std::uint32_t generation; ///< Generation of the current occupant, or of the next one while free.
        std::uint32_t dense;      ///< Index into m_values, or kNone while free.
        std::uint32_t nextFree;
    };

    /// Marks a slot free: bumps its generation and pushes it on the free list, or retires it at MaxGeneration.
    void Release(std::uint32_t slotIndex) noexcept
    {
        Slot& slot = m_slots[slotIndex];
        slot.dense = kNone;
        if (slot.generation >= MaxGeneration)
        {
            ++m_retired;
            return;
        }
        ++slot.generation;
        slot.nextFree = m_freeHead;
        m_freeHead = slotIndex;
    }

    std::vector<T, StlAllocator<T>> m_values;
    std::vector<std::uint32_t, StlAllocator<std::uint32_t>> m_denseToSlot;
    std::vector<Slot, StlAllocator<Slot>> m_slots;
    std::uint32_t m_freeHead = kNone;
    std::size_t m_retired = 0;
};

} // namespace interi

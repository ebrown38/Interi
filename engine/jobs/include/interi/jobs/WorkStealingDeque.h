// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/memory/Allocator.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace interi
{

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

/// Chase-Lev work-stealing deque (Lê, Pop, Cohen, Zappa Nardelli, "Correct and Efficient Work-Stealing for Weak
/// Memory Models", 2013) with a fixed power-of-two capacity. One owner thread calls Push/Pop at the bottom (LIFO);
/// any thread may Steal from the top (FIFO).
///
/// The paper's fences are replaced by seq_cst operations on `top`/`bottom` so ThreadSanitizer, which does not model
/// standalone fences, sees every synchronisation edge. On x86 the cost is the same.
///
/// T must be trivially copyable and lock-free as std::atomic<T> (pointers, small integers). The deque never grows:
/// Push fails when it is full, so no buffer is ever reclaimed while a thief may be reading it.
template <typename T>
class WorkStealingDeque
{
    static_assert(std::is_trivially_copyable_v<T>, "WorkStealingDeque elements must be trivially copyable");
    static_assert(std::atomic<T>::is_always_lock_free, "WorkStealingDeque elements must be lock-free atomics");

public:
    explicit WorkStealingDeque(std::size_t capacity, IAllocator& allocator = GetDefaultAllocator())
        : m_allocator(&allocator)
        , m_capacity(std::bit_ceil(std::max<std::size_t>(capacity, 2)))
        , m_mask(static_cast<std::int64_t>(m_capacity - 1))
    {
        void* memory = allocator.Allocate(m_capacity * sizeof(Slot), alignof(Slot));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        m_slots = static_cast<Slot*>(memory);
        std::uninitialized_default_construct_n(m_slots, m_capacity);
    }

    ~WorkStealingDeque()
    {
        std::destroy_n(m_slots, m_capacity);
        m_allocator->Free(m_slots, m_capacity * sizeof(Slot));
    }

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;
    WorkStealingDeque(WorkStealingDeque&&) = delete;
    WorkStealingDeque& operator=(WorkStealingDeque&&) = delete;

    /// Owner only. Adds `value` at the bottom; returns false (adding nothing) when the deque is full.
    bool Push(T value) noexcept
    {
        const std::int64_t bottom = m_bottom.load(std::memory_order_relaxed);
        const std::int64_t top = m_top.load(std::memory_order_acquire);
        if (bottom - top >= static_cast<std::int64_t>(m_capacity))
        {
            return false;
        }
        m_slots[bottom & m_mask].store(value, std::memory_order_relaxed);
        m_bottom.store(bottom + 1, std::memory_order_release); // publishes the slot (and what it points to)
        return true;
    }

    /// Owner only. Removes the most recently pushed element, or returns nothing when the deque is empty.
    std::optional<T> Pop() noexcept
    {
        const std::int64_t bottom = m_bottom.load(std::memory_order_relaxed) - 1;
        m_bottom.store(bottom, std::memory_order_seq_cst); // reserve the slot before reading top (Dekker with Steal)
        std::int64_t top = m_top.load(std::memory_order_seq_cst);
        if (top > bottom)
        {
            m_bottom.store(bottom + 1, std::memory_order_relaxed); // was empty
            return std::nullopt;
        }
        T value = m_slots[bottom & m_mask].load(std::memory_order_relaxed);
        if (top == bottom)
        {
            // Last element: race the thieves for it through top.
            const bool won =
                m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
            m_bottom.store(bottom + 1, std::memory_order_relaxed);
            if (!won)
            {
                return std::nullopt;
            }
        }
        return value;
    }

    /// Any thread. Removes the oldest element. Returns nothing when the deque is empty or another thread won the
    /// race for the same element; callers treat both as "try elsewhere".
    std::optional<T> Steal() noexcept
    {
        std::int64_t top = m_top.load(std::memory_order_seq_cst);
        const std::int64_t bottom = m_bottom.load(std::memory_order_seq_cst);
        if (top >= bottom)
        {
            return std::nullopt;
        }
        const T value = m_slots[top & m_mask].load(std::memory_order_relaxed);
        if (!m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            return std::nullopt;
        }
        return value;
    }

    /// Number of elements at some recent moment; exact only when no other thread is using the deque.
    [[nodiscard]] std::size_t SizeApprox() const noexcept
    {
        const std::int64_t bottom = m_bottom.load(std::memory_order_relaxed);
        const std::int64_t top = m_top.load(std::memory_order_relaxed);
        return bottom > top ? static_cast<std::size_t>(bottom - top) : 0;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

private:
    using Slot = std::atomic<T>;

    IAllocator* m_allocator;
    Slot* m_slots = nullptr;
    std::size_t m_capacity;
    std::int64_t m_mask;
    alignas(kCacheLineSize) std::atomic<std::int64_t> m_top{0};
    alignas(kCacheLineSize) std::atomic<std::int64_t> m_bottom{0};
    std::byte m_padding[kCacheLineSize - sizeof(std::atomic<std::int64_t>)]{};
};

INTERI_MSVC_WARNING_POP()

} // namespace interi

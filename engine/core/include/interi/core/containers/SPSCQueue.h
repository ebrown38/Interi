// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/memory/Allocator.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace interi
{

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

/// Bounded wait-free single-producer/single-consumer ring. Exactly one thread may push and one other thread may
/// pop. Each side caches the other side's index and only re-reads the shared atomic when the cache says the ring
/// is full/empty, which keeps cache-line traffic low. Capacity is rounded up to a power of two (minimum 2).
template <typename T>
class SPSCQueue
{
public:
    explicit SPSCQueue(std::size_t capacity, IAllocator& allocator = GetDefaultAllocator())
        : m_allocator(&allocator)
        , m_capacity(std::bit_ceil(std::max<std::size_t>(capacity, 2)))
        , m_mask(m_capacity - 1)
    {
        void* memory = allocator.Allocate(m_capacity * sizeof(T), alignof(T));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        m_slots = static_cast<T*>(memory);
    }

    /// Destroys any elements still queued. Neither thread may be using the queue.
    ~SPSCQueue()
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        for (std::size_t head = m_head.load(std::memory_order_relaxed); head != tail; ++head)
        {
            std::destroy_at(&m_slots[head & m_mask]);
        }
        m_allocator->Free(m_slots, m_capacity * sizeof(T));
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    bool TryPush(const T& value) { return TryEmplace(value); }
    bool TryPush(T&& value) { return TryEmplace(std::move(value)); }

    /// Producer thread only. Returns false if the ring is full.
    template <typename... Args>
    bool TryEmplace(Args&&... args)
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_cachedHead == m_capacity)
        {
            m_cachedHead = m_head.load(std::memory_order_acquire);
            if (tail - m_cachedHead == m_capacity)
            {
                return false;
            }
        }
        ::new (static_cast<void*>(&m_slots[tail & m_mask])) T(std::forward<Args>(args)...);
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Consumer thread only. Returns false if the ring is empty.
    bool TryPop(T& out)
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_cachedTail)
        {
            m_cachedTail = m_tail.load(std::memory_order_acquire);
            if (head == m_cachedTail)
            {
                return false;
            }
        }
        T* element = &m_slots[head & m_mask];
        out = std::move(*element);
        std::destroy_at(element);
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

private:
    IAllocator* m_allocator;
    T* m_slots = nullptr;
    std::size_t m_capacity;
    std::size_t m_mask;

    // Producer side.
    alignas(kCacheLineSize) std::atomic<std::size_t> m_tail{0};
    std::size_t m_cachedHead = 0;

    // Consumer side.
    alignas(kCacheLineSize) std::atomic<std::size_t> m_head{0};
    std::size_t m_cachedTail = 0;
    std::byte m_padding[kCacheLineSize - 2 * sizeof(std::size_t)]{};
};

INTERI_MSVC_WARNING_POP()

} // namespace interi

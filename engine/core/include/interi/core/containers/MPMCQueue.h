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

/// Bounded lock-free multi-producer/multi-consumer queue (Dmitry Vyukov's sequence-number design). Each cell
/// carries a sequence number that says whether it is ready to be written or read for a given lap, so producers
/// and consumers only contend on their own position counter. Used by the job system.
///
/// TryPush/TryPop never block; they fail when the queue is full/empty. FIFO per producer. Capacity is rounded up
/// to a power of two (minimum 2). TryPush can also fail below capacity, while a consumer that claimed the cell one
/// lap earlier has not finished popping it.
template <typename T>
class MPMCQueue
{
public:
    explicit MPMCQueue(std::size_t capacity, IAllocator& allocator = GetDefaultAllocator())
        : m_allocator(&allocator)
        , m_capacity(std::bit_ceil(std::max<std::size_t>(capacity, 2)))
        , m_mask(m_capacity - 1)
    {
        void* memory = allocator.Allocate(m_capacity * sizeof(Cell), alignof(Cell));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        m_cells = static_cast<Cell*>(memory);
        for (std::size_t i = 0; i < m_capacity; ++i)
        {
            ::new (static_cast<void*>(&m_cells[i])) Cell();
            m_cells[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    /// Destroys any elements still queued. No other thread may be using the queue.
    ~MPMCQueue()
    {
        const std::size_t end = m_enqueuePosition.load(std::memory_order_relaxed);
        for (std::size_t position = m_dequeuePosition.load(std::memory_order_relaxed); position != end; ++position)
        {
            std::destroy_at(std::launder(reinterpret_cast<T*>(m_cells[position & m_mask].storage)));
        }
        std::destroy_n(m_cells, m_capacity);
        m_allocator->Free(m_cells, m_capacity * sizeof(Cell));
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;

    bool TryPush(const T& value) { return TryEmplace(value); }
    bool TryPush(T&& value) { return TryEmplace(std::move(value)); }

    /// Constructs an element in place; returns false (constructing nothing) if the queue is full. Thread-safe.
    template <typename... Args>
    bool TryEmplace(Args&&... args)
    {
        std::size_t position = m_enqueuePosition.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = m_cells[position & m_mask];
            const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(sequence - position);
            if (diff == 0)
            {
                if (m_enqueuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    ::new (static_cast<void*>(cell.storage)) T(std::forward<Args>(args)...);
                    cell.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false; // full: this cell still holds the previous lap's element
            }
            else
            {
                position = m_enqueuePosition.load(std::memory_order_relaxed);
            }
        }
    }

    /// Moves the oldest available element into `out`; returns false if the queue is empty. Thread-safe.
    bool TryPop(T& out)
    {
        std::size_t position = m_dequeuePosition.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = m_cells[position & m_mask];
            const std::size_t sequence = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(sequence - (position + 1));
            if (diff == 0)
            {
                if (m_dequeuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    T* element = std::launder(reinterpret_cast<T*>(cell.storage));
                    out = std::move(*element);
                    std::destroy_at(element);
                    cell.sequence.store(position + m_capacity, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false; // empty: the producer for this position has not published yet
            }
            else
            {
                position = m_dequeuePosition.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }

private:
    struct Cell
    {
        std::atomic<std::size_t> sequence{0};
        alignas(T) std::byte storage[sizeof(T)];
    };

    IAllocator* m_allocator;
    Cell* m_cells = nullptr;
    std::size_t m_capacity;
    std::size_t m_mask;
    alignas(kCacheLineSize) std::atomic<std::size_t> m_enqueuePosition{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> m_dequeuePosition{0};
    std::byte m_padding[kCacheLineSize - sizeof(std::atomic<std::size_t>)]{};
};

INTERI_MSVC_WARNING_POP()

} // namespace interi

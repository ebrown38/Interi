// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/memory/Allocator.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace interi
{

/// Vector that stores up to N elements inline and moves to an IAllocator heap buffer beyond that. Use it for
/// short lists that are usually small (a chunk's bonds, a contact's points) to avoid heap traffic.
///
/// T must be nothrow-move-constructible. Interface follows std::vector naming.
template <typename T, std::size_t N>
class SmallVector
{
    static_assert(N > 0, "use std::vector when no inline capacity is wanted");
    static_assert(std::is_nothrow_move_constructible_v<T>, "SmallVector relocates elements with noexcept moves");

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    SmallVector() noexcept
        : SmallVector(GetDefaultAllocator())
    {
    }
    explicit SmallVector(IAllocator& allocator) noexcept
        : m_data(InlineData())
        , m_allocator(&allocator)
    {
    }
    SmallVector(std::initializer_list<T> values, IAllocator& allocator = GetDefaultAllocator())
        : SmallVector(allocator)
    {
        reserve(values.size());
        for (const T& value : values)
        {
            push_back(value);
        }
    }
    SmallVector(const SmallVector& other)
        : SmallVector(*other.m_allocator)
    {
        reserve(other.m_size);
        std::uninitialized_copy(other.begin(), other.end(), m_data);
        m_size = other.m_size;
    }
    SmallVector(SmallVector&& other) noexcept
        : SmallVector(*other.m_allocator)
    {
        TakeFrom(other);
    }
    SmallVector& operator=(const SmallVector& other)
    {
        if (this != &other)
        {
            clear();
            reserve(other.m_size);
            std::uninitialized_copy(other.begin(), other.end(), m_data);
            m_size = other.m_size;
        }
        return *this;
    }
    SmallVector& operator=(SmallVector&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            ReleaseHeap();
            m_allocator = other.m_allocator;
            TakeFrom(other);
        }
        return *this;
    }
    ~SmallVector()
    {
        clear();
        ReleaseHeap();
    }

    template <typename... Args>
    T& emplace_back(Args&&... args)
    {
        if (m_size == m_capacity)
        {
            // Construct the new element in the new buffer first: args may refer to an element of the old buffer.
            const std::size_t newCapacity = std::max(m_capacity * 2, m_size + 1);
            T* newData = AllocateBuffer(newCapacity);
            ::new (static_cast<void*>(newData + m_size)) T(std::forward<Args>(args)...);
            std::uninitialized_move(m_data, m_data + m_size, newData);
            std::destroy(m_data, m_data + m_size);
            ReleaseHeap();
            m_data = newData;
            m_capacity = newCapacity;
        }
        else
        {
            ::new (static_cast<void*>(m_data + m_size)) T(std::forward<Args>(args)...);
        }
        return m_data[m_size++];
    }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    void pop_back() noexcept
    {
        INTERI_ASSERT(m_size > 0);
        std::destroy_at(m_data + --m_size);
    }

    /// Removes the element at `position`, shifting later elements down. Returns the iterator after it.
    iterator erase(const_iterator position)
    {
        INTERI_ASSERT(position >= begin() && position < end());
        T* target = m_data + (position - m_data);
        std::move(target + 1, end(), target);
        pop_back();
        return target;
    }

    void reserve(std::size_t capacity)
    {
        if (capacity <= m_capacity)
        {
            return;
        }
        T* newData = AllocateBuffer(capacity);
        std::uninitialized_move(m_data, m_data + m_size, newData);
        std::destroy(m_data, m_data + m_size);
        ReleaseHeap();
        m_data = newData;
        m_capacity = capacity;
    }

    void resize(std::size_t size)
    {
        if (size < m_size)
        {
            std::destroy(m_data + size, m_data + m_size);
            m_size = size;
            return;
        }
        reserve(size);
        std::uninitialized_value_construct(m_data + m_size, m_data + size);
        m_size = size;
    }

    void clear() noexcept
    {
        std::destroy(m_data, m_data + m_size);
        m_size = 0;
    }

    [[nodiscard]] T& operator[](std::size_t index) noexcept
    {
        INTERI_ASSERT(index < m_size);
        return m_data[index];
    }
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept
    {
        INTERI_ASSERT(index < m_size);
        return m_data[index];
    }
    [[nodiscard]] T& front() noexcept { return (*this)[0]; }
    [[nodiscard]] T& back() noexcept { return (*this)[m_size - 1]; }
    [[nodiscard]] const T& front() const noexcept { return (*this)[0]; }
    [[nodiscard]] const T& back() const noexcept { return (*this)[m_size - 1]; }

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    /// True while the elements live in the inline buffer.
    [[nodiscard]] bool IsInline() const noexcept { return m_data == InlineData(); }

    [[nodiscard]] iterator begin() noexcept { return m_data; }
    [[nodiscard]] iterator end() noexcept { return m_data + m_size; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_data; }
    [[nodiscard]] const_iterator end() const noexcept { return m_data + m_size; }

private:
    [[nodiscard]] T* InlineData() noexcept { return reinterpret_cast<T*>(m_inline); }
    [[nodiscard]] const T* InlineData() const noexcept { return reinterpret_cast<const T*>(m_inline); }

    T* AllocateBuffer(std::size_t capacity)
    {
        void* memory = m_allocator->Allocate(capacity * sizeof(T), alignof(T));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        return static_cast<T*>(memory);
    }

    void ReleaseHeap() noexcept
    {
        if (!IsInline())
        {
            m_allocator->Free(m_data, m_capacity * sizeof(T));
            m_data = InlineData();
            m_capacity = N;
        }
    }

    /// Takes other's contents (steals a heap buffer, moves inline elements) and leaves it empty and inline.
    /// Requires *this to be empty, inline and using other's allocator.
    void TakeFrom(SmallVector& other) noexcept
    {
        if (other.IsInline())
        {
            std::uninitialized_move(other.begin(), other.end(), m_data);
            m_size = other.m_size;
            other.clear();
            return;
        }
        m_data = std::exchange(other.m_data, other.InlineData());
        m_size = std::exchange(other.m_size, 0);
        m_capacity = std::exchange(other.m_capacity, N);
    }

    T* m_data;
    std::size_t m_size = 0;
    std::size_t m_capacity = N;
    IAllocator* m_allocator;
    alignas(T) std::byte m_inline[N * sizeof(T)];
};

} // namespace interi

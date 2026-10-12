// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>

namespace interi
{

/// Vector with a fixed inline capacity and no heap use at all. Exceeding the capacity is a programming error
/// (asserted); use the try_ variants where running out is an expected condition.
/// Interface follows std::vector naming.
template <typename T, std::size_t N>
class FixedVector
{
    static_assert(N > 0);

public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    FixedVector() noexcept = default;
    FixedVector(std::initializer_list<T> values)
    {
        INTERI_ASSERT(values.size() <= N, "too many initializers");
        for (const T& value : values)
        {
            push_back(value);
        }
    }
    FixedVector(const FixedVector& other)
    {
        std::uninitialized_copy(other.begin(), other.end(), data());
        m_size = other.m_size;
    }
    FixedVector(FixedVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        std::uninitialized_move(other.begin(), other.end(), data());
        m_size = other.m_size;
        other.clear();
    }
    FixedVector& operator=(const FixedVector& other)
    {
        if (this != &other)
        {
            clear();
            std::uninitialized_copy(other.begin(), other.end(), data());
            m_size = other.m_size;
        }
        return *this;
    }
    FixedVector& operator=(FixedVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        if (this != &other)
        {
            clear();
            std::uninitialized_move(other.begin(), other.end(), data());
            m_size = other.m_size;
            other.clear();
        }
        return *this;
    }
    ~FixedVector() { clear(); }

    template <typename... Args>
    T& emplace_back(Args&&... args)
    {
        INTERI_ASSERT(m_size < N, "FixedVector capacity exceeded");
        T* slot = ::new (static_cast<void*>(data() + m_size)) T(std::forward<Args>(args)...);
        ++m_size;
        return *slot;
    }
    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    /// Appends unless full; returns whether it did.
    template <typename... Args>
    bool try_emplace_back(Args&&... args)
    {
        if (m_size == N)
        {
            return false;
        }
        emplace_back(std::forward<Args>(args)...);
        return true;
    }
    bool try_push_back(const T& value) { return try_emplace_back(value); }

    void pop_back() noexcept
    {
        INTERI_ASSERT(m_size > 0);
        std::destroy_at(data() + --m_size);
    }

    iterator erase(const_iterator position)
    {
        INTERI_ASSERT(position >= begin() && position < end());
        T* target = data() + (position - data());
        std::move(target + 1, end(), target);
        pop_back();
        return target;
    }

    void resize(std::size_t size)
    {
        INTERI_ASSERT(size <= N, "FixedVector capacity exceeded");
        if (size < m_size)
        {
            std::destroy(data() + size, data() + m_size);
        }
        else
        {
            std::uninitialized_value_construct(data() + m_size, data() + size);
        }
        m_size = size;
    }

    void clear() noexcept
    {
        std::destroy(data(), data() + m_size);
        m_size = 0;
    }

    [[nodiscard]] T& operator[](std::size_t index) noexcept
    {
        INTERI_ASSERT(index < m_size);
        return data()[index];
    }
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept
    {
        INTERI_ASSERT(index < m_size);
        return data()[index];
    }
    [[nodiscard]] T& front() noexcept { return (*this)[0]; }
    [[nodiscard]] T& back() noexcept { return (*this)[m_size - 1]; }
    [[nodiscard]] const T& front() const noexcept { return (*this)[0]; }
    [[nodiscard]] const T& back() const noexcept { return (*this)[m_size - 1]; }

    [[nodiscard]] T* data() noexcept { return reinterpret_cast<T*>(m_storage); }
    [[nodiscard]] const T* data() const noexcept { return reinterpret_cast<const T*>(m_storage); }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return m_size == N; }

    [[nodiscard]] iterator begin() noexcept { return data(); }
    [[nodiscard]] iterator end() noexcept { return data() + m_size; }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] const_iterator end() const noexcept { return data() + m_size; }

private:
    alignas(T) std::byte m_storage[N * sizeof(T)];
    std::size_t m_size = 0;
};

} // namespace interi

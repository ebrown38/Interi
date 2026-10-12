// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

namespace interi
{

/// Adapts an IAllocator to the standard Allocator requirements, so std::vector and friends can draw from engine
/// allocators: `std::vector<T, StlAllocator<T>> values{StlAllocator<T>(pool)};`
template <typename T>
class StlAllocator
{
public:
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    StlAllocator() noexcept
        : m_allocator(&GetDefaultAllocator())
    {
    }
    StlAllocator(IAllocator& allocator) noexcept // NOLINT(google-explicit-constructor): implicit like std::pmr
        : m_allocator(&allocator)
    {
    }
    template <typename U>
    StlAllocator(const StlAllocator<U>& other) noexcept // NOLINT(google-explicit-constructor): rebinding
        : m_allocator(other.Get())
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        {
            throw std::bad_array_new_length();
        }
        void* memory = m_allocator->Allocate(count * sizeof(T), alignof(T));
        if (memory == nullptr)
        {
            throw std::bad_alloc();
        }
        return static_cast<T*>(memory);
    }

    void deallocate(T* ptr, std::size_t count) noexcept { m_allocator->Free(ptr, count * sizeof(T)); }

    [[nodiscard]] IAllocator* Get() const noexcept { return m_allocator; }

    template <typename U>
    friend bool operator==(const StlAllocator& a, const StlAllocator<U>& b) noexcept
    {
        return a.Get() == b.Get();
    }

private:
    IAllocator* m_allocator;
};

} // namespace interi

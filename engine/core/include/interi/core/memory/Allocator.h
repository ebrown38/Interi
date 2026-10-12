// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace interi
{

/// Rounds value up to a multiple of alignment (a power of two).
[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] inline bool IsAligned(const void* ptr, std::size_t alignment) noexcept
{
    return (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1)) == 0;
}

/// Engine allocator interface. Containers and systems take an IAllocator& (or pointer) so memory can be
/// routed to pools, frame arenas or tracked heaps.
///
/// Free() takes the size that was passed to Allocate(), so wrappers such as TrackedAllocator need no
/// per-allocation headers. Implementations document their own thread-safety.
class IAllocator
{
public:
    virtual ~IAllocator() = default;

    /// Returns at least `size` bytes aligned to `alignment` (a power of two), or nullptr if the request cannot
    /// be satisfied. Never throws.
    [[nodiscard]] virtual void* Allocate(std::size_t size, std::size_t alignment) noexcept = 0;

    /// Releases memory from Allocate(). `size` must be the size originally requested. nullptr is ignored.
    virtual void Free(void* ptr, std::size_t size) noexcept = 0;

protected:
    IAllocator() = default;
    IAllocator(const IAllocator&) = default;
    IAllocator& operator=(const IAllocator&) = default;
    IAllocator(IAllocator&&) = default;
    IAllocator& operator=(IAllocator&&) = default;
};

/// The process-wide system heap (HeapAllocator). Thread-safe.
[[nodiscard]] IAllocator& GetDefaultAllocator() noexcept;

/// Allocates and constructs a T. Returns nullptr if the allocation fails; a throwing constructor frees the
/// memory and rethrows.
template <typename T, typename... Args>
[[nodiscard]] T* New(IAllocator& allocator, Args&&... args)
{
    void* memory = allocator.Allocate(sizeof(T), alignof(T));
    if (memory == nullptr)
    {
        return nullptr;
    }
    try
    {
        return ::new (memory) T(std::forward<Args>(args)...);
    }
    catch (...)
    {
        allocator.Free(memory, sizeof(T));
        throw;
    }
}

/// Destroys and frees an object made by New(). T must be the object's dynamic type. nullptr is ignored.
template <typename T>
void Delete(IAllocator& allocator, T* object) noexcept
{
    if (object != nullptr)
    {
        object->~T();
        allocator.Free(object, sizeof(T));
    }
}

} // namespace interi

// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/memory/HeapAllocator.h>

#include <algorithm>
#include <bit>
#include <cstdlib>

namespace interi
{

void* HeapAllocator::Allocate(std::size_t size, std::size_t alignment) noexcept
{
    INTERI_ASSERT(std::has_single_bit(alignment), "alignment must be a power of two");
    alignment = std::max(alignment, alignof(std::max_align_t));
    size = std::max<std::size_t>(size, 1);
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    return posix_memalign(&ptr, alignment, size) == 0 ? ptr : nullptr;
#endif
}

void HeapAllocator::Free(void* ptr, std::size_t /*size*/) noexcept
{
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr); // NOLINT(cppcoreguidelines-no-malloc): pairs with posix_memalign
#endif
}

IAllocator& GetDefaultAllocator() noexcept
{
    static HeapAllocator heap;
    return heap;
}

} // namespace interi

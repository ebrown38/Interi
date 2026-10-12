// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

namespace interi
{

/// Aligned system heap (posix_memalign / _aligned_malloc). Thread-safe. Usually reached through
/// GetDefaultAllocator() rather than instantiated.
class HeapAllocator final : public IAllocator
{
public:
    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    void Free(void* ptr, std::size_t size) noexcept override;
};

} // namespace interi

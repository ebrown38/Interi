// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace interi
{

/// Fixed-size block allocator (used for debris chunks and other many-equal-objects cases).
///
/// Allocate() and Free() are lock-free and thread-safe: the free list is a Treiber stack whose head packs a
/// block index with a version tag (preventing ABA). Next-links live in a separate array rather than inside free
/// blocks, so a thread reading a stale link never races with the new owner writing the block.
class PoolAllocator final : public IAllocator
{
public:
    /// `blockSize` is rounded up to a multiple of `alignment`.
    PoolAllocator(std::size_t blockSize, std::size_t blockCount, std::size_t alignment = 16,
                  IAllocator& backing = GetDefaultAllocator());
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    /// Returns one block, or nullptr when the pool is exhausted. `size`/`alignment` must fit the block.
    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    void Free(void* ptr, std::size_t size) noexcept override;

    /// True if `ptr` points into this pool's storage.
    [[nodiscard]] bool Owns(const void* ptr) const noexcept;
    /// Index of the block containing `ptr` (which must be owned), in [0, BlockCount()).
    [[nodiscard]] std::size_t IndexOf(const void* ptr) const noexcept;

    [[nodiscard]] std::size_t BlockSize() const noexcept { return m_blockSize; }
    [[nodiscard]] std::size_t BlockCount() const noexcept { return m_blockCount; }
    /// Blocks currently free. Exact when no allocation is in flight.
    [[nodiscard]] std::size_t FreeCount() const noexcept { return m_freeCount.load(std::memory_order_relaxed); }

private:
    static constexpr std::uint32_t kNone = 0xFFFF'FFFF;

    IAllocator* m_backing;
    std::size_t m_blockSize;
    std::size_t m_blockCount;
    std::size_t m_alignment;
    std::byte* m_storage = nullptr;
    std::unique_ptr<std::atomic<std::uint32_t>[]> m_next;
    std::atomic<std::uint64_t> m_head; ///< (version << 32) | first free index
    std::atomic<std::size_t> m_freeCount;
};

} // namespace interi

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace interi
{

/// General-purpose heap over one fixed region using Two-Level Segregated Fit (Masmano et al., 2004):
/// O(1) allocate and free, bounded fragmentation, immediate coalescing of neighbouring free blocks.
///
/// Each block has a 16-byte header; payloads are 16-byte aligned, and larger alignments are served by splitting
/// off a leading free block. Not thread-safe: give each thread or system its own instance, or guard it.
class TlsfAllocator final : public IAllocator
{
public:
    /// Manages `capacity` bytes (rounded down to 16; at most 2^40) reserved from `backing`.
    explicit TlsfAllocator(std::size_t capacity, IAllocator& backing = GetDefaultAllocator());
    ~TlsfAllocator() override;

    TlsfAllocator(const TlsfAllocator&) = delete;
    TlsfAllocator& operator=(const TlsfAllocator&) = delete;
    TlsfAllocator(TlsfAllocator&&) = delete;
    TlsfAllocator& operator=(TlsfAllocator&&) = delete;

    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    /// `size` is not needed by TLSF and may be 0.
    void Free(void* ptr, std::size_t size) noexcept override;

    /// Sum of free payload bytes (O(free blocks); intended for stats and tests).
    [[nodiscard]] std::size_t FreeBytes() const noexcept;
    /// Payload size of the largest free block. An allocation of exactly this size can still fail: TLSF rounds
    /// requests up to the next size class ("good fit") so it never has to search a list.
    [[nodiscard]] std::size_t LargestFreeBlock() const noexcept;
    /// Walks the heap and checks every structural invariant. O(n^2) worst case; for tests and debugging.
    [[nodiscard]] bool Validate() const noexcept;

    struct Block; ///< Implementation detail (block header layout), public only for the .cpp's helpers.

private:
    static constexpr unsigned kSlLog2 = 4;
    static constexpr unsigned kSlCount = 1u << kSlLog2;
    static constexpr unsigned kFlCount = 34; ///< Covers block sizes below 2^41.

    static void Mapping(std::size_t size, unsigned& fl, unsigned& sl) noexcept;
    void InsertFree(Block* block) noexcept;
    void RemoveFree(Block* block) noexcept;
    /// Removes and returns a free block of at least `size` payload bytes, or nullptr.
    Block* TakeFreeBlock(std::size_t size) noexcept;
    /// Splits off any tail beyond `size` as a new free block and marks `block` used.
    void TrimAndMarkUsed(Block* block, std::size_t size) noexcept;
    [[nodiscard]] Block* FirstBlock() const noexcept;

    IAllocator* m_backing;
    std::byte* m_memory = nullptr;
    std::size_t m_capacity;
    std::uint64_t m_flBitmap = 0;
    std::array<std::uint32_t, kFlCount> m_slBitmaps{};
    std::array<std::array<Block*, kSlCount>, kFlCount> m_freeLists{};
};

} // namespace interi

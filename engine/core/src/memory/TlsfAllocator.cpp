// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/memory/TlsfAllocator.h>

#include <algorithm>
#include <bit>
#include <new>

namespace interi
{

/// Physical block header. The free-list links occupy the first 16 payload bytes and are only valid while the
/// block is free. Block sizes count payload bytes only and are multiples of kAlign, leaving the low bits for flags.
struct TlsfAllocator::Block
{
    Block* prevPhysical; ///< Always maintained (16-byte header either way, so no footer trick is needed).
    std::size_t sizeAndFlags;
    Block* nextFree;
    Block* prevFree;
};

namespace
{
constexpr std::size_t kAlign = 16;
constexpr std::size_t kHeaderSize = 16; // prevPhysical + sizeAndFlags
constexpr std::size_t kMinPayload = 16; // nextFree + prevFree
constexpr std::size_t kFreeBit = 1;
constexpr std::size_t kPrevFreeBit = 2;
constexpr std::size_t kFlagMask = kAlign - 1;
constexpr unsigned kSmallShift = 8; // sizes below 256 map to first level 0 in 16-byte steps
constexpr std::size_t kMaxCapacity = std::size_t{1} << 40;

static_assert(offsetof(TlsfAllocator::Block, nextFree) == kHeaderSize);
} // namespace

namespace
{
using Block = TlsfAllocator::Block;

std::size_t SizeOf(const Block* block) noexcept
{
    return block->sizeAndFlags & ~kFlagMask;
}
void SetSize(Block* block, std::size_t size) noexcept
{
    block->sizeAndFlags = size | (block->sizeAndFlags & kFlagMask);
}
bool IsFree(const Block* block) noexcept
{
    return (block->sizeAndFlags & kFreeBit) != 0;
}
bool IsPrevFree(const Block* block) noexcept
{
    return (block->sizeAndFlags & kPrevFreeBit) != 0;
}
void SetFlag(Block* block, std::size_t flag, bool value) noexcept
{
    block->sizeAndFlags = value ? (block->sizeAndFlags | flag) : (block->sizeAndFlags & ~flag);
}
std::byte* PayloadOf(Block* block) noexcept
{
    return reinterpret_cast<std::byte*>(block) + kHeaderSize;
}
Block* BlockFromPayload(void* payload) noexcept
{
    return reinterpret_cast<Block*>(static_cast<std::byte*>(payload) - kHeaderSize);
}
Block* NextPhysical(const Block* block) noexcept
{
    return reinterpret_cast<Block*>(reinterpret_cast<std::byte*>(const_cast<Block*>(block)) + kHeaderSize +
                                    SizeOf(block));
}
/// Writes a block header at `at`. Only the 16 header bytes are touched: the sentinel block has no payload.
Block* MakeBlock(std::byte* at, Block* prevPhysical, std::size_t size, std::size_t flags) noexcept
{
    auto* block = reinterpret_cast<Block*>(at);
    block->prevPhysical = prevPhysical;
    block->sizeAndFlags = size | flags;
    return block;
}
} // namespace

TlsfAllocator::TlsfAllocator(std::size_t capacity, IAllocator& backing)
    : m_backing(&backing)
    , m_capacity(capacity & ~(kAlign - 1))
{
    INTERI_ASSERT(m_capacity <= kMaxCapacity, "TLSF capacity above 2^40");
    INTERI_ASSERT(m_capacity >= 2 * kHeaderSize + kMinPayload, "TLSF capacity too small");
    m_memory = static_cast<std::byte*>(backing.Allocate(m_capacity, kAlign));
    if (m_memory == nullptr)
    {
        throw std::bad_alloc();
    }
    // One free block spanning the region, followed by a zero-size, always-used sentinel that stops coalescing.
    Block* first = MakeBlock(m_memory, nullptr, m_capacity - 2 * kHeaderSize, kFreeBit);
    MakeBlock(m_memory + m_capacity - kHeaderSize, first, 0, kPrevFreeBit);
    InsertFree(first);
}

TlsfAllocator::~TlsfAllocator()
{
    m_backing->Free(m_memory, m_capacity);
}

void TlsfAllocator::Mapping(std::size_t size, unsigned& fl, unsigned& sl) noexcept
{
    if (size < (std::size_t{1} << kSmallShift))
    {
        fl = 0;
        sl = static_cast<unsigned>(size / kAlign);
        return;
    }
    const auto msb = static_cast<unsigned>(std::bit_width(size) - 1);
    fl = msb - kSmallShift + 1;
    sl = static_cast<unsigned>(size >> (msb - kSlLog2)) - kSlCount;
}

void TlsfAllocator::InsertFree(Block* block) noexcept
{
    unsigned fl = 0;
    unsigned sl = 0;
    Mapping(SizeOf(block), fl, sl);
    Block*& head = m_freeLists[fl][sl];
    block->prevFree = nullptr;
    block->nextFree = head;
    if (head != nullptr)
    {
        head->prevFree = block;
    }
    head = block;
    m_flBitmap |= std::uint64_t{1} << fl;
    m_slBitmaps[fl] |= 1u << sl;
}

void TlsfAllocator::RemoveFree(Block* block) noexcept
{
    unsigned fl = 0;
    unsigned sl = 0;
    Mapping(SizeOf(block), fl, sl);
    if (block->prevFree != nullptr)
    {
        block->prevFree->nextFree = block->nextFree;
    }
    else
    {
        m_freeLists[fl][sl] = block->nextFree;
    }
    if (block->nextFree != nullptr)
    {
        block->nextFree->prevFree = block->prevFree;
    }
    if (m_freeLists[fl][sl] == nullptr)
    {
        m_slBitmaps[fl] &= ~(1u << sl);
        if (m_slBitmaps[fl] == 0)
        {
            m_flBitmap &= ~(std::uint64_t{1} << fl);
        }
    }
}

TlsfAllocator::Block* TlsfAllocator::TakeFreeBlock(std::size_t size) noexcept
{
    if (size > m_capacity)
    {
        return nullptr;
    }
    // Round up to the next list boundary so every block in the chosen list is large enough ("good fit").
    if (size >= (std::size_t{1} << kSmallShift))
    {
        const auto msb = static_cast<unsigned>(std::bit_width(size) - 1);
        size += (std::size_t{1} << (msb - kSlLog2)) - 1;
    }
    unsigned fl = 0;
    unsigned sl = 0;
    Mapping(size, fl, sl);
    if (fl >= kFlCount)
    {
        return nullptr;
    }

    std::uint32_t slMap = m_slBitmaps[fl] & (~0u << sl);
    if (slMap == 0)
    {
        const std::uint64_t flMap = m_flBitmap & (~std::uint64_t{0} << (fl + 1));
        if (flMap == 0)
        {
            return nullptr;
        }
        fl = static_cast<unsigned>(std::countr_zero(flMap));
        slMap = m_slBitmaps[fl];
    }
    sl = static_cast<unsigned>(std::countr_zero(slMap));
    Block* block = m_freeLists[fl][sl];
    RemoveFree(block);
    return block;
}

void TlsfAllocator::TrimAndMarkUsed(Block* block, std::size_t size) noexcept
{
    const std::size_t blockSize = SizeOf(block);
    if (blockSize >= size + kHeaderSize + kMinPayload)
    {
        Block* remainder =
            MakeBlock(PayloadOf(block) + size, block, blockSize - size - kHeaderSize, kFreeBit /* prev used */);
        SetSize(block, size);
        Block* next = NextPhysical(remainder);
        next->prevPhysical = remainder;
        SetFlag(next, kPrevFreeBit, true);
        InsertFree(remainder);
    }
    else
    {
        SetFlag(NextPhysical(block), kPrevFreeBit, false);
    }
    SetFlag(block, kFreeBit, false);
}

void* TlsfAllocator::Allocate(std::size_t size, std::size_t alignment) noexcept
{
    INTERI_ASSERT(std::has_single_bit(alignment), "alignment must be a power of two");
    if (size > m_capacity)
    {
        return nullptr;
    }
    const std::size_t payload = std::max(AlignUp(std::max<std::size_t>(size, 1), kAlign), kMinPayload);

    if (alignment <= kAlign)
    {
        Block* block = TakeFreeBlock(payload);
        if (block == nullptr)
        {
            return nullptr;
        }
        TrimAndMarkUsed(block, payload);
        return PayloadOf(block);
    }

    // Over-aligned: find room for the payload plus a worst-case leading gap, which must itself be able to form a
    // free block (header + minimum payload) whenever it is non-zero.
    constexpr std::size_t kMinGap = kHeaderSize + kMinPayload;
    Block* block = TakeFreeBlock(payload + alignment + kMinGap);
    if (block == nullptr)
    {
        return nullptr;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(PayloadOf(block));
    std::size_t gap = AlignUp(start, alignment) - start;
    if (gap != 0 && gap < kMinGap)
    {
        gap = AlignUp(start + kMinGap, alignment) - start;
    }
    if (gap != 0)
    {
        // Split the gap off the front as its own free block. Its physical predecessor is used (free blocks are
        // always coalesced), so it never needs merging here.
        const std::size_t originalSize = SizeOf(block);
        Block* aligned = MakeBlock(PayloadOf(block) + gap - kHeaderSize, block, originalSize - gap, kPrevFreeBit);
        SetSize(block, gap - kHeaderSize);
        NextPhysical(aligned)->prevPhysical = aligned;
        InsertFree(block);
        block = aligned;
    }
    TrimAndMarkUsed(block, payload);
    return PayloadOf(block);
}

void TlsfAllocator::Free(void* ptr, std::size_t /*size*/) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }
    INTERI_ASSERT(ptr >= m_memory && ptr < m_memory + m_capacity, "pointer does not belong to this heap");
    Block* block = BlockFromPayload(ptr);
    INTERI_ASSERT(!IsFree(block), "double free");
    SetFlag(block, kFreeBit, true);

    if (IsPrevFree(block))
    {
        Block* prev = block->prevPhysical;
        RemoveFree(prev);
        SetSize(prev, SizeOf(prev) + kHeaderSize + SizeOf(block));
        block = prev;
    }
    Block* next = NextPhysical(block);
    if (IsFree(next))
    {
        RemoveFree(next);
        SetSize(block, SizeOf(block) + kHeaderSize + SizeOf(next));
        next = NextPhysical(block);
    }
    next->prevPhysical = block;
    SetFlag(next, kPrevFreeBit, true);
    InsertFree(block);
}

TlsfAllocator::Block* TlsfAllocator::FirstBlock() const noexcept
{
    return reinterpret_cast<Block*>(m_memory);
}

std::size_t TlsfAllocator::FreeBytes() const noexcept
{
    std::size_t total = 0;
    for (const auto& level : m_freeLists)
    {
        for (const Block* block : level)
        {
            for (; block != nullptr; block = block->nextFree)
            {
                total += SizeOf(block);
            }
        }
    }
    return total;
}

std::size_t TlsfAllocator::LargestFreeBlock() const noexcept
{
    if (m_flBitmap == 0)
    {
        return 0;
    }
    const auto fl = static_cast<unsigned>(std::bit_width(m_flBitmap) - 1);
    const auto sl = static_cast<unsigned>(std::bit_width(m_slBitmaps[fl]) - 1);
    std::size_t largest = 0;
    for (const Block* block = m_freeLists[fl][sl]; block != nullptr; block = block->nextFree)
    {
        largest = std::max(largest, SizeOf(block));
    }
    return largest;
}

bool TlsfAllocator::Validate() const noexcept
{
    // Physical walk: sizes, links, flags, coalescing.
    std::size_t physicalFree = 0;
    const Block* prev = nullptr;
    const Block* block = FirstBlock();
    const auto* end = reinterpret_cast<const Block*>(m_memory + m_capacity - kHeaderSize);
    while (block != end)
    {
        if (block > end || !IsAligned(block, kAlign) || block->prevPhysical != prev)
        {
            return false;
        }
        if (prev != nullptr && IsPrevFree(block) != IsFree(prev))
        {
            return false;
        }
        if (IsFree(block))
        {
            if (prev != nullptr && IsFree(prev))
            {
                return false; // two adjacent free blocks: missed coalesce
            }
            unsigned fl = 0;
            unsigned sl = 0;
            Mapping(SizeOf(block), fl, sl);
            bool listed = false;
            for (const Block* entry = m_freeLists[fl][sl]; entry != nullptr; entry = entry->nextFree)
            {
                listed = listed || entry == block;
            }
            if (!listed)
            {
                return false;
            }
            ++physicalFree;
        }
        if (SizeOf(block) < kMinPayload)
        {
            return false;
        }
        prev = block;
        block = NextPhysical(block);
    }
    if (end->prevPhysical != prev || IsFree(end) || IsPrevFree(end) != IsFree(prev))
    {
        return false;
    }

    // Free lists: back links, bitmaps, and the same number of free blocks as the physical walk.
    std::size_t listedFree = 0;
    for (unsigned fl = 0; fl < kFlCount; ++fl)
    {
        for (unsigned sl = 0; sl < kSlCount; ++sl)
        {
            const Block* head = m_freeLists[fl][sl];
            const bool bit = (m_slBitmaps[fl] & (1u << sl)) != 0;
            if (bit != (head != nullptr))
            {
                return false;
            }
            const Block* back = nullptr;
            for (const Block* entry = head; entry != nullptr; entry = entry->nextFree)
            {
                if (entry->prevFree != back || !IsFree(entry))
                {
                    return false;
                }
                back = entry;
                ++listedFree;
            }
        }
        if (((m_flBitmap >> fl) & 1u) != (m_slBitmaps[fl] != 0 ? 1u : 0u))
        {
            return false;
        }
    }
    return listedFree == physicalFree;
}

} // namespace interi

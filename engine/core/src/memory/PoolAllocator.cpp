// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/memory/PoolAllocator.h>

#include <bit>
#include <new>

namespace interi
{

namespace
{
constexpr std::uint64_t PackHead(std::uint64_t version, std::uint32_t index) noexcept
{
    return (version << 32) | index;
}
constexpr std::uint32_t HeadIndex(std::uint64_t head) noexcept
{
    return static_cast<std::uint32_t>(head);
}
constexpr std::uint64_t HeadVersion(std::uint64_t head) noexcept
{
    return head >> 32;
}
} // namespace

PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t blockCount, std::size_t alignment, IAllocator& backing)
    : m_backing(&backing)
    , m_blockSize(AlignUp(blockSize == 0 ? 1 : blockSize, alignment))
    , m_blockCount(blockCount)
    , m_alignment(alignment)
    , m_next(std::make_unique<std::atomic<std::uint32_t>[]>(blockCount))
    , m_head(PackHead(0, blockCount == 0 ? kNone : 0))
    , m_freeCount(blockCount)
{
    INTERI_ASSERT(std::has_single_bit(alignment), "alignment must be a power of two");
    INTERI_ASSERT(blockCount < kNone, "too many blocks");
    m_storage = static_cast<std::byte*>(backing.Allocate(m_blockSize * blockCount, alignment));
    if (m_storage == nullptr)
    {
        throw std::bad_alloc();
    }
    for (std::size_t i = 0; i < blockCount; ++i)
    {
        m_next[i].store(i + 1 < blockCount ? static_cast<std::uint32_t>(i + 1) : kNone, std::memory_order_relaxed);
    }
}

PoolAllocator::~PoolAllocator()
{
    m_backing->Free(m_storage, m_blockSize * m_blockCount);
}

void* PoolAllocator::Allocate(std::size_t size, std::size_t alignment) noexcept
{
    INTERI_ASSERT(size <= m_blockSize && alignment <= m_alignment, "request does not fit a pool block");
    std::uint64_t head = m_head.load(std::memory_order_acquire);
    for (;;)
    {
        const std::uint32_t index = HeadIndex(head);
        if (index == kNone)
        {
            return nullptr;
        }
        // May read a link that is already stale; the versioned CAS below then fails and we retry.
        const std::uint32_t next = m_next[index].load(std::memory_order_relaxed);
        if (m_head.compare_exchange_weak(head, PackHead(HeadVersion(head) + 1, next), std::memory_order_acquire,
                                         std::memory_order_acquire))
        {
            m_freeCount.fetch_sub(1, std::memory_order_relaxed);
            return m_storage + static_cast<std::size_t>(index) * m_blockSize;
        }
    }
}

void PoolAllocator::Free(void* ptr, std::size_t /*size*/) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }
    INTERI_ASSERT(Owns(ptr), "pointer does not belong to this pool");
    const auto index = static_cast<std::uint32_t>(IndexOf(ptr));
    std::uint64_t head = m_head.load(std::memory_order_relaxed);
    do
    {
        m_next[index].store(HeadIndex(head), std::memory_order_relaxed);
    } while (!m_head.compare_exchange_weak(head, PackHead(HeadVersion(head) + 1, index), std::memory_order_release,
                                           std::memory_order_relaxed));
    m_freeCount.fetch_add(1, std::memory_order_relaxed);
}

bool PoolAllocator::Owns(const void* ptr) const noexcept
{
    const auto* bytes = static_cast<const std::byte*>(ptr);
    return bytes >= m_storage && bytes < m_storage + m_blockSize * m_blockCount;
}

std::size_t PoolAllocator::IndexOf(const void* ptr) const noexcept
{
    const auto offset = static_cast<std::size_t>(static_cast<const std::byte*>(ptr) - m_storage);
    INTERI_ASSERT(offset % m_blockSize == 0, "pointer is not the start of a block");
    return offset / m_blockSize;
}

} // namespace interi

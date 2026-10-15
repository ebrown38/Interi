// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/DebrisPool.h>

#include <algorithm>
#include <cstddef>

namespace interi::destruction
{

namespace
{

template <typename T>
T* AllocateArray(IAllocator& allocator, std::uint32_t count)
{
    if (count == 0)
    {
        return nullptr;
    }
    void* memory = allocator.Allocate(sizeof(T) * count, alignof(T));
    INTERI_VERIFY(memory != nullptr);
    return static_cast<T*>(memory);
}

template <typename T>
void FreeArray(IAllocator& allocator, T* array, std::uint32_t count) noexcept
{
    if (array != nullptr)
    {
        allocator.Free(array, sizeof(T) * count);
    }
}

} // namespace

DebrisPool::DebrisPool(std::uint32_t capacity, IAllocator& allocator)
    : m_allocator(&allocator)
    , m_capacity(capacity)
    , m_freeCount(capacity)
    , m_priorities(AllocateArray<float>(allocator, capacity))
    , m_users(AllocateArray<std::uint64_t>(allocator, capacity))
    , m_generations(AllocateArray<std::uint32_t>(allocator, capacity))
    , m_free(AllocateArray<std::uint32_t>(allocator, capacity))
    , m_live(AllocateArray<std::uint8_t>(allocator, capacity))
{
    for (std::uint32_t index = 0; index < capacity; ++index)
    {
        m_priorities[index] = 0.0f;
        m_users[index] = 0;
        m_generations[index] = 1;
        m_free[index] = capacity - 1 - index;
        m_live[index] = 0;
    }
}

DebrisPool::~DebrisPool()
{
    FreeArray(*m_allocator, m_priorities, m_capacity);
    FreeArray(*m_allocator, m_users, m_capacity);
    FreeArray(*m_allocator, m_generations, m_capacity);
    FreeArray(*m_allocator, m_free, m_capacity);
    FreeArray(*m_allocator, m_live, m_capacity);
}

std::uint32_t DebrisPool::LowestPriorityLive() const noexcept
{
    std::uint32_t lowest = 0;
    for (std::uint32_t index = 1; index < m_capacity; ++index)
    {
        if (m_priorities[index] < m_priorities[lowest])
        {
            lowest = index;
        }
    }
    return lowest;
}

DebrisPool::Acquired DebrisPool::Acquire(std::uint64_t user, float priority) noexcept
{
    Acquired result;
    std::uint32_t index = 0;
    if (m_freeCount > 0)
    {
        index = m_free[--m_freeCount];
        m_live[index] = 1;
        ++m_liveCount;
    }
    else
    {
        if (m_capacity == 0)
        {
            return result;
        }
        // Full: every slot is live.
        index = LowestPriorityLive();
        if (priority < m_priorities[index])
        {
            return result;
        }
        result.evicted = m_users[index];
        m_generations[index] = std::max(m_generations[index] + 1, 1u);
    }
    m_users[index] = user;
    m_priorities[index] = priority;
    result.slot = DebrisSlot{index, m_generations[index]};
    return result;
}

bool DebrisPool::Release(DebrisSlot slot) noexcept
{
    if (!IsLive(slot))
    {
        return false;
    }
    m_live[slot.index] = 0;
    m_generations[slot.index] = std::max(m_generations[slot.index] + 1, 1u);
    m_free[m_freeCount++] = slot.index;
    --m_liveCount;
    return true;
}

bool DebrisPool::IsLive(DebrisSlot slot) const noexcept
{
    return slot.IsValid() && slot.index < m_capacity && m_live[slot.index] != 0 &&
           m_generations[slot.index] == slot.generation;
}

std::uint64_t DebrisPool::User(DebrisSlot slot) const noexcept
{
    INTERI_ASSERT(IsLive(slot));
    return m_users[slot.index];
}

float DebrisPool::Priority(DebrisSlot slot) const noexcept
{
    INTERI_ASSERT(IsLive(slot));
    return m_priorities[slot.index];
}

void DebrisPool::SetPriority(DebrisSlot slot, float priority) noexcept
{
    INTERI_ASSERT(IsLive(slot));
    m_priorities[slot.index] = priority;
}

DebrisSlot DebrisPool::SlotAt(std::uint32_t index) const noexcept
{
    if (index >= m_capacity || m_live[index] == 0)
    {
        return {};
    }
    return DebrisSlot{index, m_generations[index]};
}

} // namespace interi::destruction

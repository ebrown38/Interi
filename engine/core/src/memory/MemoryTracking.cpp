// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/memory/MemoryTracking.h>

#include <array>
#include <atomic>
#include <mutex>

namespace interi
{

namespace
{
struct TagCounters
{
    std::atomic<std::uint64_t> currentBytes{0};
    std::atomic<std::uint64_t> peakBytes{0};
    std::atomic<std::uint64_t> allocationCount{0};
    std::atomic<std::uint64_t> freeCount{0};
    std::atomic<std::uint64_t> budgetBytes{0};
    std::atomic<std::uint64_t> overruns{0};
};

std::array<TagCounters, static_cast<std::size_t>(MemoryTag::Count)> g_counters;

std::mutex g_policyMutex;
OutOfBudgetPolicy g_policy;

BudgetAction DefaultPolicy(const BudgetViolation&, void*) noexcept
{
    return BudgetAction::Allow; // counted in GetBudgetOverruns; formatting a log line here could throw
}

TagCounters& CountersFor(MemoryTag tag) noexcept
{
    INTERI_ASSERT(tag < MemoryTag::Count);
    return g_counters[static_cast<std::size_t>(tag)];
}
} // namespace

std::string_view ToString(MemoryTag tag) noexcept
{
    switch (tag)
    {
    case MemoryTag::General:
        return "General";
    case MemoryTag::Core:
        return "Core";
    case MemoryTag::Jobs:
        return "Jobs";
    case MemoryTag::Physics:
        return "Physics";
    case MemoryTag::Destruction:
        return "Destruction";
    case MemoryTag::Render:
        return "Render";
    case MemoryTag::Audio:
        return "Audio";
    case MemoryTag::Assets:
        return "Assets";
    case MemoryTag::Gameplay:
        return "Gameplay";
    case MemoryTag::Editor:
        return "Editor";
    case MemoryTag::Count:
        break;
    }
    return "?";
}

MemoryTagStats GetMemoryStats(MemoryTag tag) noexcept
{
    const TagCounters& counters = CountersFor(tag);
    return {.currentBytes = counters.currentBytes.load(std::memory_order_relaxed),
            .peakBytes = counters.peakBytes.load(std::memory_order_relaxed),
            .allocationCount = counters.allocationCount.load(std::memory_order_relaxed),
            .freeCount = counters.freeCount.load(std::memory_order_relaxed)};
}

void RecordAllocation(MemoryTag tag, std::size_t bytes) noexcept
{
    TagCounters& counters = CountersFor(tag);
    const std::uint64_t current = counters.currentBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    counters.allocationCount.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t peak = counters.peakBytes.load(std::memory_order_relaxed);
    while (current > peak && !counters.peakBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed))
    {
    }
}

void RecordFree(MemoryTag tag, std::size_t bytes) noexcept
{
    TagCounters& counters = CountersFor(tag);
    counters.currentBytes.fetch_sub(bytes, std::memory_order_relaxed);
    counters.freeCount.fetch_add(1, std::memory_order_relaxed);
}

void SetMemoryBudget(MemoryTag tag, std::uint64_t bytes) noexcept
{
    CountersFor(tag).budgetBytes.store(bytes, std::memory_order_relaxed);
}

std::uint64_t GetMemoryBudget(MemoryTag tag) noexcept
{
    return CountersFor(tag).budgetBytes.load(std::memory_order_relaxed);
}

std::uint64_t GetBudgetOverruns(MemoryTag tag) noexcept
{
    return CountersFor(tag).overruns.load(std::memory_order_relaxed);
}

OutOfBudgetPolicy SetOutOfBudgetPolicy(OutOfBudgetPolicy policy) noexcept
{
    const std::scoped_lock lock(g_policyMutex);
    const OutOfBudgetPolicy previous = g_policy;
    g_policy = policy;
    return previous;
}

void* TrackedAllocator::Allocate(std::size_t size, std::size_t alignment) noexcept
{
    TagCounters& counters = CountersFor(m_tag);
    const std::uint64_t budget = counters.budgetBytes.load(std::memory_order_relaxed);
    if (budget != 0)
    {
        const std::uint64_t current = counters.currentBytes.load(std::memory_order_relaxed);
        if (current + size > budget)
        {
            counters.overruns.fetch_add(1, std::memory_order_relaxed);
            OutOfBudgetPolicy policy;
            {
                const std::scoped_lock lock(g_policyMutex);
                policy = g_policy;
            }
            const BudgetViolation violation{m_tag, budget, current, size};
            const BudgetAction action =
                policy.handler != nullptr ? policy.handler(violation, policy.user) : DefaultPolicy(violation, nullptr);
            if (action == BudgetAction::Deny)
            {
                return nullptr;
            }
        }
    }
    void* ptr = m_inner->Allocate(size, alignment);
    if (ptr != nullptr)
    {
        RecordAllocation(m_tag, size);
    }
    return ptr;
}

void TrackedAllocator::Free(void* ptr, std::size_t size) noexcept
{
    if (ptr != nullptr)
    {
        RecordFree(m_tag, size);
        m_inner->Free(ptr, size);
    }
}

} // namespace interi

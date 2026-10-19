// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace interi
{

/// Budget category an allocation is charged to.
enum class MemoryTag : std::uint8_t
{
    General,
    Core,
    Jobs,
    Physics,
    Destruction,
    Render,
    Audio,
    Assets,
    Gameplay,
    Editor,
    Count
};

[[nodiscard]] std::string_view ToString(MemoryTag tag) noexcept;

/// Snapshot of one tag's counters.
struct MemoryTagStats
{
    std::uint64_t currentBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t allocationCount = 0; ///< Lifetime total.
    std::uint64_t freeCount = 0;       ///< Lifetime total.
};

/// Process-wide counters per tag. All functions are thread-safe (relaxed atomics; peak is a CAS high-water mark).
[[nodiscard]] MemoryTagStats GetMemoryStats(MemoryTag tag) noexcept;
void RecordAllocation(MemoryTag tag, std::size_t bytes) noexcept;
void RecordFree(MemoryTag tag, std::size_t bytes) noexcept;

/// What an allocation that would push a tag past its budget does.
enum class BudgetAction : std::uint8_t
{
    Allow, ///< Go ahead anyway (the overrun is still counted in MemoryBudgetStats::overruns).
    Deny   ///< TrackedAllocator::Allocate returns nullptr.
};

struct BudgetViolation
{
    MemoryTag tag = MemoryTag::General;
    std::uint64_t budgetBytes = 0;
    std::uint64_t currentBytes = 0;
    std::uint64_t requestedBytes = 0;
};

/// The out-of-budget policy. Called on the allocating thread, before the allocation, so it must be quick, must not
/// allocate through a TrackedAllocator and must be thread-safe. A game or module may free caches in it (and answer
/// Allow) or refuse. The default logs the first overrun per tag and answers Allow.
using OutOfBudgetHandler = BudgetAction (*)(const BudgetViolation& violation, void* user) noexcept;

/// A tag's budget in bytes of live allocations; 0 (the default) is unlimited. Only TrackedAllocator enforces it.
void SetMemoryBudget(MemoryTag tag, std::uint64_t bytes) noexcept;
[[nodiscard]] std::uint64_t GetMemoryBudget(MemoryTag tag) noexcept;
/// Allocations that went over budget (denied or not), lifetime total.
[[nodiscard]] std::uint64_t GetBudgetOverruns(MemoryTag tag) noexcept;
struct OutOfBudgetPolicy
{
    OutOfBudgetHandler handler = nullptr; ///< nullptr: the default policy.
    void* user = nullptr;
};

/// Replaces the policy and returns the previous one. A module that installs a handler must put the previous one back
/// before its library unloads (ScopedOutOfBudgetHandler).
OutOfBudgetPolicy SetOutOfBudgetPolicy(OutOfBudgetPolicy policy) noexcept;

class ScopedOutOfBudgetHandler
{
public:
    explicit ScopedOutOfBudgetHandler(OutOfBudgetHandler handler, void* user = nullptr) noexcept
        : m_previous(SetOutOfBudgetPolicy({handler, user}))
    {
    }
    ~ScopedOutOfBudgetHandler() { SetOutOfBudgetPolicy(m_previous); }
    ScopedOutOfBudgetHandler(const ScopedOutOfBudgetHandler&) = delete;
    ScopedOutOfBudgetHandler& operator=(const ScopedOutOfBudgetHandler&) = delete;

private:
    OutOfBudgetPolicy m_previous;
};

/// Forwards to another allocator and charges successful allocations to a tag. Counts requested bytes (not
/// padding). Thread-safe if the inner allocator is.
class TrackedAllocator final : public IAllocator
{
public:
    TrackedAllocator(IAllocator& inner, MemoryTag tag) noexcept
        : m_inner(&inner)
        , m_tag(tag)
    {
    }

    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept override;
    void Free(void* ptr, std::size_t size) noexcept override;

    [[nodiscard]] MemoryTag Tag() const noexcept { return m_tag; }

private:
    IAllocator* m_inner;
    MemoryTag m_tag;
};

} // namespace interi

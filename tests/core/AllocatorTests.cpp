// SPDX-License-Identifier: MIT
#include <interi/core/memory/FrameAllocator.h>
#include <interi/core/memory/HeapAllocator.h>
#include <interi/core/memory/LinearAllocator.h>
#include <interi/core/memory/MemoryTracking.h>
#include <interi/core/memory/PoolAllocator.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

namespace
{
bool IsAligned(const void* ptr, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}
} // namespace

// --- HeapAllocator ---------------------------------------------------------------------------------------------------

TEST(HeapAllocator, OverAlignedAllocation_IsAligned)
{
    interi::IAllocator& heap = interi::GetDefaultAllocator();
    void* ptr = heap.Allocate(100, 512);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(IsAligned(ptr, 512));
    heap.Free(ptr, 100);
}

TEST(HeapAllocator, NewDelete_ConstructsAndDestroys)
{
    struct Counted
    {
        explicit Counted(int& liveCounter)
            : counter(liveCounter)
        {
            ++counter;
        }
        ~Counted() { --counter; }
        int& counter;
    };
    int live = 0;
    Counted* object = interi::New<Counted>(interi::GetDefaultAllocator(), live);
    EXPECT_EQ(live, 1);
    interi::Delete(interi::GetDefaultAllocator(), object);
    EXPECT_EQ(live, 0);
}

// --- LinearAllocator -------------------------------------------------------------------------------------------------

TEST(LinearAllocator, Reset_ReclaimsAll)
{
    interi::LinearAllocator linear(1024);
    void* first = linear.Allocate(1000, 1);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(linear.Allocate(100, 1), nullptr) << "exhausted";
    EXPECT_EQ(linear.Used(), 1000u);

    linear.Reset();
    EXPECT_EQ(linear.Used(), 0u);
    EXPECT_EQ(linear.Allocate(1000, 1), first) << "the same memory is handed out again";
}

TEST(LinearAllocator, Rewind_ReleasesOnlyLaterAllocations)
{
    interi::LinearAllocator linear(256);
    ASSERT_NE(linear.Allocate(16, 16), nullptr);
    const interi::LinearAllocator::Marker marker = linear.GetMarker();
    void* scratch = linear.Allocate(64, 16);
    ASSERT_NE(scratch, nullptr);
    linear.Rewind(marker);
    EXPECT_EQ(linear.Used(), 16u);
    EXPECT_EQ(linear.Allocate(64, 16), scratch);
}

class LinearAllocatorAlignment : public ::testing::TestWithParam<std::size_t>
{
};

TEST_P(LinearAllocatorAlignment, Alignment_Respected)
{
    const std::size_t alignment = GetParam();
    interi::LinearAllocator linear(8192);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_NE(linear.Allocate(1, 1), nullptr); // misalign the cursor
        void* ptr = linear.Allocate(24, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(IsAligned(ptr, alignment)) << "alignment " << alignment;
    }
}

INSTANTIATE_TEST_SUITE_P(PowersOfTwo, LinearAllocatorAlignment, ::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256));

// --- PoolAllocator ---------------------------------------------------------------------------------------------------

TEST(PoolAllocator, ExhaustThenFree_Reuses)
{
    interi::PoolAllocator pool(48, 4);
    std::vector<void*> blocks;
    for (int i = 0; i < 4; ++i)
    {
        void* block = pool.Allocate(48, 16);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }
    EXPECT_EQ(std::set<void*>(blocks.begin(), blocks.end()).size(), 4u) << "blocks are distinct";
    EXPECT_EQ(pool.Allocate(48, 16), nullptr) << "pool exhausted";
    EXPECT_EQ(pool.FreeCount(), 0u);

    pool.Free(blocks[2], 48);
    EXPECT_EQ(pool.FreeCount(), 1u);
    EXPECT_EQ(pool.Allocate(48, 16), blocks[2]);
}

TEST(PoolAllocator, Blocks_AreAlignedAndDoNotOverlap)
{
    interi::PoolAllocator pool(40, 8, 64);
    std::vector<void*> blocks;
    for (int i = 0; i < 8; ++i)
    {
        void* block = pool.Allocate(40, 64);
        ASSERT_NE(block, nullptr);
        EXPECT_TRUE(IsAligned(block, 64));
        std::memset(block, i, 40);
        blocks.push_back(block);
    }
    for (int i = 0; i < 8; ++i)
    {
        const auto* bytes = static_cast<const unsigned char*>(blocks[static_cast<std::size_t>(i)]);
        for (int b = 0; b < 40; ++b)
        {
            ASSERT_EQ(bytes[b], static_cast<unsigned char>(i));
        }
    }
    EXPECT_EQ(pool.BlockSize(), 64u) << "block size is rounded up to the alignment";
}

TEST(PoolAllocator, Owns_RecognisesItsBlocks)
{
    interi::PoolAllocator pool(32, 2);
    void* block = pool.Allocate(32, 8);
    int outside = 0;
    EXPECT_TRUE(pool.Owns(block));
    EXPECT_FALSE(pool.Owns(&outside));
    pool.Free(block, 32);
}

// --- FrameAllocators -------------------------------------------------------------------------------------------------

TEST(FrameAllocators, PreviousFrameData_SurvivesOneFlip)
{
    interi::FrameAllocators frames(2, 256);
    auto* value = static_cast<int*>(frames.ForThread(0).Allocate(sizeof(int), alignof(int)));
    ASSERT_NE(value, nullptr);
    *value = 1234;

    frames.BeginFrame();
    // A second frame's allocations come from the other buffer, so last frame's data is untouched.
    auto* next = static_cast<int*>(frames.ForThread(0).Allocate(sizeof(int), alignof(int)));
    ASSERT_NE(next, nullptr);
    *next = 99;
    EXPECT_NE(next, value);
    EXPECT_EQ(*value, 1234);
}

TEST(FrameAllocators, BufferIsReset_AfterTwoFlips)
{
    interi::FrameAllocators frames(1, 256);
    void* first = frames.ForThread(0).Allocate(64, 16);
    frames.BeginFrame();
    frames.BeginFrame();
    EXPECT_EQ(frames.ForThread(0).Used(), 0u);
    EXPECT_EQ(frames.ForThread(0).Allocate(64, 16), first);
}

TEST(FrameAllocators, Threads_GetDistinctAllocators)
{
    interi::FrameAllocators frames(3, 128);
    EXPECT_EQ(frames.ThreadCount(), 3u);
    EXPECT_NE(&frames.ForThread(0), &frames.ForThread(1));
    EXPECT_NE(&frames.ForThread(1), &frames.ForThread(2));
}

// --- Memory tracking -------------------------------------------------------------------------------------------------

TEST(TrackedAllocator, Tag_StatsBalanced)
{
    const interi::MemoryTagStats before = interi::GetMemoryStats(interi::MemoryTag::Destruction);
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Destruction);

    void* a = tracked.Allocate(100, 16);
    void* b = tracked.Allocate(300, 64);
    const interi::MemoryTagStats during = interi::GetMemoryStats(interi::MemoryTag::Destruction);
    EXPECT_EQ(during.currentBytes - before.currentBytes, 400u);
    EXPECT_GE(during.peakBytes, before.currentBytes + 400u);

    tracked.Free(a, 100);
    tracked.Free(b, 300);
    const interi::MemoryTagStats after = interi::GetMemoryStats(interi::MemoryTag::Destruction);
    EXPECT_EQ(after.currentBytes, before.currentBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, 2u);
    EXPECT_EQ(after.freeCount - before.freeCount, 2u);
    EXPECT_GE(after.peakBytes, during.peakBytes) << "peak is a high-water mark";
}

TEST(TrackedAllocator, FailedAllocation_IsNotCounted)
{
    interi::LinearAllocator tiny(16);
    interi::TrackedAllocator tracked(tiny, interi::MemoryTag::Render);
    const interi::MemoryTagStats before = interi::GetMemoryStats(interi::MemoryTag::Render);
    EXPECT_EQ(tracked.Allocate(1024, 16), nullptr);
    const interi::MemoryTagStats after = interi::GetMemoryStats(interi::MemoryTag::Render);
    EXPECT_EQ(after.currentBytes, before.currentBytes);
    EXPECT_EQ(after.allocationCount, before.allocationCount);
}

TEST(MemoryTag, ToString_NamesTags)
{
    EXPECT_EQ(interi::ToString(interi::MemoryTag::Physics), "Physics");
    EXPECT_EQ(interi::ToString(interi::MemoryTag::Destruction), "Destruction");
}

// --- Memory budgets --------------------------------------------------------------------------------------------------

namespace
{
struct BudgetProbe
{
    interi::BudgetAction answer = interi::BudgetAction::Deny;
    int calls = 0;
    interi::BudgetViolation last;
};

interi::BudgetAction ProbeHandler(const interi::BudgetViolation& violation, void* user) noexcept
{
    auto* probe = static_cast<BudgetProbe*>(user);
    ++probe->calls;
    probe->last = violation;
    return probe->answer;
}
} // namespace

TEST(MemoryBudget, UnderBudget_AllocatesWithoutCallingThePolicy)
{
    BudgetProbe probe;
    const interi::ScopedOutOfBudgetHandler scoped(&ProbeHandler, &probe);
    const std::uint64_t base = interi::GetMemoryStats(interi::MemoryTag::Audio).currentBytes;
    interi::SetMemoryBudget(interi::MemoryTag::Audio, base + 1000);
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Audio);
    void* ptr = tracked.Allocate(1000, 16);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(probe.calls, 0);
    tracked.Free(ptr, 1000);
    interi::SetMemoryBudget(interi::MemoryTag::Audio, 0);
}

TEST(MemoryBudget, OverBudget_DenyReturnsNullAndCountsNothing)
{
    BudgetProbe probe;
    const interi::ScopedOutOfBudgetHandler scoped(&ProbeHandler, &probe);
    const std::uint64_t base = interi::GetMemoryStats(interi::MemoryTag::Audio).currentBytes;
    const std::uint64_t overruns = interi::GetBudgetOverruns(interi::MemoryTag::Audio);
    interi::SetMemoryBudget(interi::MemoryTag::Audio, base + 100);
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Audio);
    EXPECT_EQ(tracked.Allocate(200, 16), nullptr);
    EXPECT_EQ(probe.calls, 1);
    EXPECT_EQ(probe.last.tag, interi::MemoryTag::Audio);
    EXPECT_EQ(probe.last.requestedBytes, 200u);
    EXPECT_EQ(probe.last.budgetBytes, base + 100);
    EXPECT_EQ(interi::GetMemoryStats(interi::MemoryTag::Audio).currentBytes, base);
    EXPECT_EQ(interi::GetBudgetOverruns(interi::MemoryTag::Audio), overruns + 1);
    interi::SetMemoryBudget(interi::MemoryTag::Audio, 0);
}

TEST(MemoryBudget, OverBudget_AllowStillAllocatesAndIsCounted)
{
    BudgetProbe probe;
    probe.answer = interi::BudgetAction::Allow;
    const interi::ScopedOutOfBudgetHandler scoped(&ProbeHandler, &probe);
    interi::SetMemoryBudget(interi::MemoryTag::Audio, 1);
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Audio);
    void* ptr = tracked.Allocate(64, 16);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(probe.calls, 1);
    tracked.Free(ptr, 64);
    interi::SetMemoryBudget(interi::MemoryTag::Audio, 0);
}

TEST(MemoryBudget, ScopedHandler_RestoresThePreviousPolicy)
{
    BudgetProbe outer;
    const interi::ScopedOutOfBudgetHandler first(&ProbeHandler, &outer);
    {
        BudgetProbe inner;
        const interi::ScopedOutOfBudgetHandler second(&ProbeHandler, &inner);
        interi::SetMemoryBudget(interi::MemoryTag::Audio, 1);
        interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Audio);
        EXPECT_EQ(tracked.Allocate(64, 16), nullptr);
        EXPECT_EQ(inner.calls, 1);
        EXPECT_EQ(outer.calls, 0);
    }
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Audio);
    EXPECT_EQ(tracked.Allocate(64, 16), nullptr);
    EXPECT_EQ(outer.calls, 1) << "the outer handler is back, with its own user pointer";
    interi::SetMemoryBudget(interi::MemoryTag::Audio, 0);
}

TEST(MemoryBudget, NoBudget_IsUnlimited)
{
    EXPECT_EQ(interi::GetMemoryBudget(interi::MemoryTag::Jobs), 0u);
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Jobs);
    void* ptr = tracked.Allocate(1 << 20, 16);
    EXPECT_NE(ptr, nullptr);
    tracked.Free(ptr, 1 << 20);
}

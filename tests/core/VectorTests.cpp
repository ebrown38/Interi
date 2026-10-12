// SPDX-License-Identifier: MIT
#include <interi/core/containers/FixedVector.h>
#include <interi/core/containers/SmallVector.h>
#include <interi/core/memory/MemoryTracking.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace
{
/// Counts live instances so tests can check every constructed element is destroyed exactly once.
struct Tracked
{
    static inline int live = 0;

    explicit Tracked(int v = 0)
        : value(v)
    {
        ++live;
    }
    Tracked(const Tracked& other)
        : value(other.value)
    {
        ++live;
    }
    Tracked(Tracked&& other) noexcept
        : value(std::exchange(other.value, -1))
    {
        ++live;
    }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    ~Tracked() { --live; }

    int value;
};

class TrackedTest : public ::testing::Test
{
protected:
    void SetUp() override { Tracked::live = 0; }
    void TearDown() override { EXPECT_EQ(Tracked::live, 0) << "leaked or double-destroyed elements"; }
};

using SmallVectorTest = TrackedTest;
using FixedVectorTest = TrackedTest;
} // namespace

// --- SmallVector ----------------------------------------------------------------------------------------------------

TEST_F(SmallVectorTest, InlineThenSpills_PreservesElements)
{
    interi::SmallVector<Tracked, 4> vec;
    for (int i = 0; i < 4; ++i)
    {
        vec.emplace_back(i);
    }
    EXPECT_TRUE(vec.IsInline());
    vec.emplace_back(4);
    EXPECT_FALSE(vec.IsInline());
    ASSERT_EQ(vec.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(vec[static_cast<std::size_t>(i)].value, i);
    }
    EXPECT_EQ(Tracked::live, 5);
}

TEST_F(SmallVectorTest, InlineBuffer_UsesNoAllocator)
{
    interi::TrackedAllocator tracked(interi::GetDefaultAllocator(), interi::MemoryTag::Core);
    const auto before = interi::GetMemoryStats(interi::MemoryTag::Core).allocationCount;
    const auto bytesBefore = interi::GetMemoryStats(interi::MemoryTag::Core).currentBytes;
    {
        interi::SmallVector<int, 8> vec(tracked);
        for (int i = 0; i < 8; ++i)
        {
            vec.push_back(i);
        }
        EXPECT_EQ(interi::GetMemoryStats(interi::MemoryTag::Core).allocationCount, before);
        vec.push_back(8);
        EXPECT_EQ(interi::GetMemoryStats(interi::MemoryTag::Core).allocationCount, before + 1);
    }
    EXPECT_EQ(interi::GetMemoryStats(interi::MemoryTag::Core).currentBytes, bytesBefore) << "heap buffer freed";
}

TEST_F(SmallVectorTest, CopyAndMove_ProduceIndependentEqualVectors)
{
    interi::SmallVector<Tracked, 2> heap;
    for (int i = 0; i < 5; ++i)
    {
        heap.emplace_back(i);
    }
    interi::SmallVector<Tracked, 2> copy = heap;
    ASSERT_EQ(copy.size(), 5u);
    copy[0].value = 100;
    EXPECT_EQ(heap[0].value, 0);

    const Tracked* heapData = heap.data();
    interi::SmallVector<Tracked, 2> moved = std::move(heap);
    EXPECT_EQ(moved.data(), heapData) << "moving a spilled vector steals the buffer";
    EXPECT_TRUE(heap.empty()); // NOLINT(bugprone-use-after-move): moved-from state is specified as empty

    interi::SmallVector<Tracked, 4> inlineVec;
    inlineVec.emplace_back(7);
    interi::SmallVector<Tracked, 4> inlineMoved = std::move(inlineVec);
    ASSERT_EQ(inlineMoved.size(), 1u);
    EXPECT_EQ(inlineMoved[0].value, 7);
}

TEST_F(SmallVectorTest, ResizeEraseClear_KeepElementCountsBalanced)
{
    interi::SmallVector<Tracked, 3> vec;
    vec.resize(6);
    EXPECT_EQ(Tracked::live, 6);
    vec.resize(2);
    EXPECT_EQ(Tracked::live, 2);
    vec[0].value = 10;
    vec[1].value = 11;
    vec.emplace_back(12);
    vec.erase(vec.begin());
    ASSERT_EQ(vec.size(), 2u);
    EXPECT_EQ(vec[0].value, 11);
    EXPECT_EQ(vec[1].value, 12);
    vec.pop_back();
    EXPECT_EQ(Tracked::live, 1);
    vec.clear();
    EXPECT_EQ(Tracked::live, 0);
}

TEST(SmallVector, SelfReferencingPushBack_SurvivesGrowth)
{
    interi::SmallVector<std::string, 1> vec;
    vec.push_back("first element long enough to defeat small-string optimisation");
    vec.push_back(vec[0]); // the argument lives in the buffer being reallocated
    EXPECT_EQ(vec[1], vec[0]);
}

// --- FixedVector ----------------------------------------------------------------------------------------------------

TEST_F(FixedVectorTest, TryPushBack_FailsWhenFull)
{
    interi::FixedVector<Tracked, 3> vec;
    EXPECT_TRUE(vec.try_emplace_back(1));
    EXPECT_TRUE(vec.try_emplace_back(2));
    EXPECT_TRUE(vec.try_emplace_back(3));
    EXPECT_TRUE(vec.full());
    EXPECT_FALSE(vec.try_emplace_back(4));
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec.back().value, 3);
}

TEST_F(FixedVectorTest, CopyAndRangeFor_Work)
{
    interi::FixedVector<Tracked, 4> vec;
    vec.emplace_back(1);
    vec.emplace_back(2);
    const interi::FixedVector<Tracked, 4> copy = vec;
    int sum = 0;
    for (const Tracked& t : copy)
    {
        sum += t.value;
    }
    EXPECT_EQ(sum, 3);
    EXPECT_EQ(Tracked::live, 4);
}

TEST(FixedVector, StorageIsInline)
{
    static_assert(sizeof(interi::FixedVector<int, 16>) <= 16 * sizeof(int) + sizeof(std::size_t) * 2);
    interi::FixedVector<int, 2> vec{5, 6};
    EXPECT_EQ(vec[1], 6);
}

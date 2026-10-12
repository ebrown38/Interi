// SPDX-License-Identifier: MIT
#include <interi/core/memory/TlsfAllocator.h>
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
constexpr std::size_t kHeapBytes = 1 << 20;

bool IsAligned(const void* ptr, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}
} // namespace

TEST(TlsfAllocator, AllocateFree_RestoresFreeSpace)
{
    interi::TlsfAllocator tlsf(kHeapBytes);
    const std::size_t initialFree = tlsf.FreeBytes();
    EXPECT_GT(initialFree, kHeapBytes - 256);

    void* ptr = tlsf.Allocate(1000, 16);
    ASSERT_NE(ptr, nullptr);
    std::memset(ptr, 0xAB, 1000);
    EXPECT_LT(tlsf.FreeBytes(), initialFree - 1000);
    EXPECT_TRUE(tlsf.Validate());

    tlsf.Free(ptr, 1000);
    EXPECT_EQ(tlsf.FreeBytes(), initialFree);
    EXPECT_TRUE(tlsf.Validate());
}

TEST(TlsfAllocator, FreeInAnyOrder_CoalescesIntoOneBlock)
{
    interi::TlsfAllocator tlsf(kHeapBytes);
    const std::size_t initialLargest = tlsf.LargestFreeBlock();

    void* a = tlsf.Allocate(256, 16);
    void* b = tlsf.Allocate(512, 16);
    void* c = tlsf.Allocate(128, 16);
    ASSERT_TRUE(a != nullptr && b != nullptr && c != nullptr);

    tlsf.Free(a, 256);
    tlsf.Free(c, 128);
    EXPECT_TRUE(tlsf.Validate());
    tlsf.Free(b, 512); // merges with both neighbours
    EXPECT_TRUE(tlsf.Validate());
    EXPECT_EQ(tlsf.LargestFreeBlock(), initialLargest);
}

TEST(TlsfAllocator, FreedBlock_IsReusedForSameSize)
{
    interi::TlsfAllocator tlsf(kHeapBytes);
    void* a = tlsf.Allocate(64, 16);
    void* guard = tlsf.Allocate(64, 16); // keeps `a` from merging back into the big tail block
    tlsf.Free(a, 64);
    EXPECT_EQ(tlsf.Allocate(64, 16), a);
    tlsf.Free(guard, 64);
}

TEST(TlsfAllocator, Exhaustion_ReturnsNullptr)
{
    interi::TlsfAllocator tlsf(4096);
    EXPECT_EQ(tlsf.Allocate(8192, 16), nullptr);

    std::vector<void*> blocks;
    while (void* block = tlsf.Allocate(256, 16))
    {
        blocks.push_back(block);
    }
    EXPECT_FALSE(blocks.empty());
    EXPECT_TRUE(tlsf.Validate());
    for (void* block : blocks)
    {
        tlsf.Free(block, 256);
    }
    EXPECT_NE(tlsf.Allocate(2048, 16), nullptr) << "space is usable again after freeing";
}

TEST(TlsfAllocator, ZeroSize_ReturnsUniqueValidPointer)
{
    interi::TlsfAllocator tlsf(4096);
    void* a = tlsf.Allocate(0, 1);
    void* b = tlsf.Allocate(0, 1);
    EXPECT_NE(a, nullptr);
    EXPECT_NE(a, b);
    tlsf.Free(a, 0);
    tlsf.Free(b, 0);
    EXPECT_TRUE(tlsf.Validate());
}

class TlsfAlignment : public ::testing::TestWithParam<std::size_t>
{
};

TEST_P(TlsfAlignment, Alignment_Respected)
{
    const std::size_t alignment = GetParam();
    interi::TlsfAllocator tlsf(kHeapBytes);
    const std::size_t initialFree = tlsf.FreeBytes();
    std::vector<void*> blocks;
    for (int i = 0; i < 16; ++i)
    {
        blocks.push_back(tlsf.Allocate(24, 8)); // shift the next block off any large alignment
        void* ptr = tlsf.Allocate(100, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(IsAligned(ptr, alignment)) << "alignment " << alignment;
        std::memset(ptr, 0xCD, 100);
        blocks.push_back(ptr);
        ASSERT_TRUE(tlsf.Validate());
    }
    for (void* block : blocks)
    {
        tlsf.Free(block, 0);
    }
    EXPECT_TRUE(tlsf.Validate());
    EXPECT_EQ(tlsf.FreeBytes(), initialFree);
}

INSTANTIATE_TEST_SUITE_P(PowersOfTwo, TlsfAlignment, ::testing::Values(1, 8, 16, 32, 64, 256, 4096));

class TlsfRandomized : public interi::testing::SeededTest
{
};

TEST_F(TlsfRandomized, RandomAllocFree_KeepsInvariantsAndContents)
{
    interi::TlsfAllocator tlsf(kHeapBytes);
    const std::size_t initialFree = tlsf.FreeBytes();

    struct Live
    {
        unsigned char* ptr;
        std::size_t size;
        unsigned char fill;
    };
    std::vector<Live> live;
    for (int op = 0; op < 4000; ++op)
    {
        const bool allocate = live.empty() || Rng().NextInt(0, 99) < 55;
        if (allocate)
        {
            const auto size = static_cast<std::size_t>(Rng().NextInt(1, 4096));
            const auto alignment = std::size_t{1} << Rng().NextInt(0, 7);
            auto* ptr = static_cast<unsigned char*>(tlsf.Allocate(size, alignment));
            if (ptr == nullptr)
            {
                continue; // heap full: legal
            }
            ASSERT_TRUE(IsAligned(ptr, alignment));
            const auto fill = static_cast<unsigned char>(op);
            std::memset(ptr, fill, size);
            live.push_back({ptr, size, fill});
        }
        else
        {
            const auto index = static_cast<std::size_t>(Rng().NextInt(0, static_cast<std::int64_t>(live.size()) - 1));
            const Live victim = live[index];
            for (std::size_t i = 0; i < victim.size; ++i)
            {
                ASSERT_EQ(victim.ptr[i], victim.fill) << "block contents were overwritten";
            }
            tlsf.Free(victim.ptr, victim.size);
            live[index] = live.back();
            live.pop_back();
        }
        if (op % 64 == 0)
        {
            ASSERT_TRUE(tlsf.Validate()) << "after op " << op;
        }
    }
    for (const Live& block : live)
    {
        tlsf.Free(block.ptr, block.size);
    }
    EXPECT_TRUE(tlsf.Validate());
    EXPECT_EQ(tlsf.FreeBytes(), initialFree);
}

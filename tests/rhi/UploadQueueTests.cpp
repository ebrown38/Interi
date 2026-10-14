// SPDX-License-Identifier: MIT
#include <interi/rhi/UploadQueue.h>

#include "MockDevice.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

using interi::rhi::BufferDesc;
using interi::rhi::BufferHandle;
using interi::rhi::MemoryUsage;
using interi::rhi::UploadQueue;
using interi::testing::MockCommandList;
using interi::testing::MockDevice;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::ReturnPointee;

namespace
{

/// A device whose Upload buffers are host vectors, so tests can inspect staged bytes.
struct UploadFixture : ::testing::Test
{
    UploadFixture()
    {
        ON_CALL(device, CreateBuffer(_))
            .WillByDefault(
                [this](const BufferDesc& desc)
                {
                    EXPECT_EQ(desc.memory, MemoryUsage::Upload);
                    staging.emplace_back(desc.size);
                    return interi::Result<BufferHandle>(
                        BufferHandle::FromParts(static_cast<std::uint32_t>(staging.size() - 1), 1));
                });
        ON_CALL(device, MappedMemory(_))
            .WillByDefault([this](BufferHandle buffer) { return std::span<std::byte>(staging[buffer.Index()]); });
        ON_CALL(device, CompletedValue()).WillByDefault(ReturnPointee(&completed));
        ON_CALL(device, LastSubmittedValue()).WillByDefault(ReturnPointee(&completed));
    }

    static std::vector<std::byte> Pattern(std::size_t size)
    {
        std::vector<std::byte> data(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            data[i] = static_cast<std::byte>((i * 31u + 7u) & 0xFFu);
        }
        return data;
    }

    std::uint64_t completed = 0;
    std::deque<std::vector<std::byte>> staging;
    NiceMock<MockDevice> device;
    NiceMock<MockCommandList> commands;
    BufferHandle destination = BufferHandle::FromParts(100, 1);
};

constexpr std::uint64_t kChunk = 64 * 1024;

} // namespace

TEST_F(UploadFixture, UploadQueue_LargeBuffer_SplitsIntoStagingChunks)
{
    const auto data = Pattern(200 * 1024);
    UploadQueue queue(device, kChunk);

    {
        ::testing::InSequence sequence;
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            EXPECT_CALL(commands, CopyBuffer(BufferHandle::FromParts(i, 1), 0, destination, 1000 + i * kChunk, kChunk));
        }
        EXPECT_CALL(commands, CopyBuffer(BufferHandle::FromParts(3, 1), 0, destination, 1000 + 3 * kChunk, 8 * 1024));
    }
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 1000, data).has_value());

    ASSERT_EQ(staging.size(), 4u);
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        ASSERT_EQ(staging[i / kChunk][i % kChunk], data[i]) << "byte " << i;
    }
}

TEST_F(UploadFixture, UploadQueue_SmallUploads_ShareAChunk)
{
    const auto data = Pattern(100);
    UploadQueue queue(device, kChunk);

    EXPECT_CALL(commands, CopyBuffer(BufferHandle::FromParts(0, 1), 0, destination, 0, 100));
    // Staging offsets stay 16-byte aligned.
    EXPECT_CALL(commands, CopyBuffer(BufferHandle::FromParts(0, 1), 112, destination, 500, 100));
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 0, data).has_value());
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 500, data).has_value());
    EXPECT_EQ(staging.size(), 1u);
}

TEST_F(UploadFixture, UploadQueue_ChunkInFlight_ReusedOnlyAfterFenceCompletes)
{
    const auto data = Pattern(kChunk);
    UploadQueue queue(device, kChunk);

    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 0, data).has_value());
    queue.Submitted(1);
    // Value 1 has not completed: the first chunk is still being read by the GPU, so a new one is created.
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 0, data).has_value());
    queue.Submitted(2);
    EXPECT_EQ(staging.size(), 2u);

    completed = 1;
    EXPECT_CALL(commands, CopyBuffer(BufferHandle::FromParts(0, 1), 0, destination, 0, kChunk));
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 0, data).has_value());
    EXPECT_EQ(staging.size(), 2u);
    queue.Submitted(3);

    EXPECT_CALL(device, WaitForValue(3));
    EXPECT_CALL(device, DestroyBuffer(_)).Times(2);
}

TEST_F(UploadFixture, UploadQueue_EmptyUpload_RecordsNothing)
{
    UploadQueue queue(device, kChunk);
    EXPECT_CALL(commands, CopyBuffer(_, _, _, _, _)).Times(0);
    ASSERT_TRUE(queue.UploadBuffer(commands, destination, 0, {}).has_value());
    EXPECT_TRUE(staging.empty());
}

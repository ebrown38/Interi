// SPDX-License-Identifier: MIT
#include <interi/rhi/ResourceManager.h>

#include "MockDevice.h"
#include <gtest/gtest.h>

#include <cstdint>

using interi::rhi::BufferHandle;
using interi::rhi::ResourceManager;
using interi::rhi::TextureHandle;
using interi::testing::MockDevice;
using ::testing::NiceMock;
using ::testing::ReturnPointee;

namespace
{

struct ResourceManagerFixture : ::testing::Test
{
    ResourceManagerFixture()
    {
        ON_CALL(device, LastSubmittedValue()).WillByDefault(ReturnPointee(&submitted));
        ON_CALL(device, CompletedValue()).WillByDefault(ReturnPointee(&completed));
    }

    std::uint64_t submitted = 3;
    std::uint64_t completed = 3;
    NiceMock<MockDevice> device;
};

} // namespace

TEST_F(ResourceManagerFixture, DeferredDestroy_WaitsForFrameFence)
{
    const auto buffer = BufferHandle::FromParts(7, 1);
    ResourceManager manager(device);

    // The frame being recorded will be submitted as value 4, so the buffer may still be used until 4 completes.
    manager.DestroyBuffer(buffer);
    EXPECT_CALL(device, DestroyBuffer(buffer)).Times(0);
    manager.Collect();
    EXPECT_EQ(manager.PendingCount(), 1u);
    ::testing::Mock::VerifyAndClearExpectations(&device);

    submitted = 4;
    completed = 4;
    EXPECT_CALL(device, DestroyBuffer(buffer)).Times(1);
    manager.Collect();
    EXPECT_EQ(manager.PendingCount(), 0u);
}

TEST_F(ResourceManagerFixture, DeferredDestroy_TexturesFollowTheSameFence)
{
    const auto texture = TextureHandle::FromParts(2, 5);
    ResourceManager manager(device);
    manager.DestroyTexture(texture);

    EXPECT_CALL(device, DestroyTexture(texture)).Times(0);
    manager.Collect();
    ::testing::Mock::VerifyAndClearExpectations(&device);

    completed = 4;
    EXPECT_CALL(device, DestroyTexture(texture)).Times(1);
    manager.Collect();
}

TEST_F(ResourceManagerFixture, Collect_DestroysOnlyRetiredFrames)
{
    const auto early = BufferHandle::FromParts(1, 1);
    const auto late = BufferHandle::FromParts(2, 1);
    ResourceManager manager(device);
    manager.DestroyBuffer(early); // retires at 4
    submitted = 4;
    manager.DestroyBuffer(late); // retires at 5

    completed = 4;
    EXPECT_CALL(device, DestroyBuffer(early)).Times(1);
    EXPECT_CALL(device, DestroyBuffer(late)).Times(0);
    manager.Collect();
    EXPECT_EQ(manager.PendingCount(), 1u);
    ::testing::Mock::VerifyAndClearExpectations(&device);

    EXPECT_CALL(device, DestroyBuffer(late)).Times(1);
    // Destruction waits for the GPU and frees what is left.
    EXPECT_CALL(device, WaitForValue(5)).Times(1);
}

TEST_F(ResourceManagerFixture, Destructor_NothingPending_DoesNotWait)
{
    EXPECT_CALL(device, WaitForValue(::testing::_)).Times(0);
    const ResourceManager manager(device);
}

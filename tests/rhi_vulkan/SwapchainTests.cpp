// SPDX-License-Identifier: MIT
#include <interi/rhi/Swapchain.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <memory>

using namespace interi::rhi;

namespace
{

/// A device with headless-surface support, so swapchains work without a window (CI, lavapipe).
class SwapchainTest : public interi::testing::VulkanTest
{
protected:
    void SetUp() override
    {
        static constexpr std::array kExtensions{"VK_KHR_surface", "VK_EXT_headless_surface"};
        CreateDevice(VulkanDeviceDesc{.validation = true, .instanceExtensions = kExtensions, .swapchain = true});
    }

    /// Records a frame that only transitions the image for presentation, submits it and presents.
    void PresentFrame(ISwapchain& swapchain)
    {
        auto image = swapchain.AcquireNextImage();
        ASSERT_TRUE(image.has_value()) << image.error().message;
        auto pool = device->CreateCommandPool();
        ASSERT_TRUE(pool.has_value());
        ICommandList& commands = device->BeginCommandList(*pool);
        const std::array toPresent{TextureBarrier{*image, ResourceState::Undefined, ResourceState::Present}};
        commands.Barriers({}, toPresent);
        ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device->Submit(lists, swapchain);
        ASSERT_TRUE(swapchain.Present().has_value());
        device->WaitForValue(value);
        device->DestroyCommandPool(*pool);
    }
};

} // namespace

TEST_F(SwapchainTest, Swapchain_HeadlessSurface_AcquireAndPresent)
{
    auto surface = device->CreateHeadlessSurface();
    ASSERT_TRUE(surface.has_value()) << surface.error().message;
    auto swapchain = device->CreateSwapchain(*surface, SwapchainDesc{.width = 64, .height = 48});
    ASSERT_TRUE(swapchain.has_value()) << swapchain.error().message;
    EXPECT_EQ((*swapchain)->Width(), 64u);
    EXPECT_EQ((*swapchain)->Height(), 48u);
    EXPECT_GE((*swapchain)->ImageCount(), 2u);
    EXPECT_NE((*swapchain)->ImageFormat(), Format::Unknown);

    for (int frame = 0; frame < 5; ++frame)
    {
        PresentFrame(**swapchain);
    }
}

TEST_F(SwapchainTest, Swapchain_Resize_Recreates)
{
    auto surface = device->CreateHeadlessSurface();
    ASSERT_TRUE(surface.has_value()) << surface.error().message;
    auto created = device->CreateSwapchain(*surface, SwapchainDesc{.width = 64, .height = 48});
    ASSERT_TRUE(created.has_value()) << created.error().message;
    ISwapchain& swapchain = **created;
    PresentFrame(swapchain);

    swapchain.Resize(128, 96);
    // The new size applies when the next image is acquired.
    PresentFrame(swapchain);
    EXPECT_EQ(swapchain.Width(), 128u);
    EXPECT_EQ(swapchain.Height(), 96u);
    PresentFrame(swapchain);
}

TEST_F(SwapchainTest, Swapchain_ZeroSize_AcquireFailsUntilResized)
{
    auto surface = device->CreateHeadlessSurface();
    ASSERT_TRUE(surface.has_value());
    auto created = device->CreateSwapchain(*surface, SwapchainDesc{.width = 32, .height = 32});
    ASSERT_TRUE(created.has_value());
    ISwapchain& swapchain = **created;

    // A minimised window has no drawable area: there is nothing to render into.
    swapchain.Resize(0, 0);
    EXPECT_FALSE(swapchain.AcquireNextImage().has_value());
    swapchain.Resize(16, 16);
    PresentFrame(swapchain);
}

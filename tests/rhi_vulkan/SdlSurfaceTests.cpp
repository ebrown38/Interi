// SPDX-License-Identifier: MIT
// SDL window -> Vulkan surface -> swapchain, end to end. SDL's "offscreen" video driver creates windows without a
// display and backs their Vulkan surfaces with VK_EXT_headless_surface, so this runs headless.
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/rhi/Swapchain.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

using namespace interi;
using namespace interi::rhi;

namespace
{

class SdlSurfaceTest : public interi::testing::VulkanTest
{
protected:
    void SetUp() override
    {
        auto platform = SdlPlatform::Create(SdlPlatformDesc{.videoDriver = "offscreen"});
        ASSERT_TRUE(platform.has_value()) << platform.error().message;
        m_platform = std::move(*platform);
        auto window = m_platform->OpenWindow(WindowDesc{.title = "surface test", .size = {96, 64}, .vulkan = true});
        if (!window.has_value())
        {
            GTEST_SKIP() << "SDL video driver has no Vulkan support: " << window.error().message;
        }
        m_window = std::move(*window);
        auto extensions = m_platform->VulkanInstanceExtensions();
        if (!extensions.has_value())
        {
            GTEST_SKIP() << "SDL video driver has no Vulkan support: " << extensions.error().message;
        }
        m_extensions = std::move(*extensions);
        CreateDevice(VulkanDeviceDesc{.validation = true, .instanceExtensions = m_extensions, .swapchain = true});
    }

    void TearDown() override
    {
        VulkanTest::TearDown(); // the device goes before the window and platform
        m_window.reset();
        m_platform.reset();
    }

    std::unique_ptr<SdlPlatform> m_platform;
    std::unique_ptr<IWindow> m_window;
    std::vector<const char*> m_extensions;
};

} // namespace

TEST_F(SdlSurfaceTest, SdlWindow_VulkanSurface_PresentsFrames)
{
    auto surface = m_platform->CreateVulkanSurface(*m_window, device->NativeInstance());
    ASSERT_TRUE(surface.has_value()) << surface.error().message;
    const WindowExtent size = m_window->Size();
    auto swapchain = device->CreateSwapchain(*surface, SwapchainDesc{.width = size.width, .height = size.height});
    ASSERT_TRUE(swapchain.has_value()) << swapchain.error().message;
    auto pool = device->CreateCommandPool();
    ASSERT_TRUE(pool.has_value());

    for (int frame = 0; frame < 3; ++frame)
    {
        device->ResetCommandPool(*pool);
        auto image = (*swapchain)->AcquireNextImage();
        ASSERT_TRUE(image.has_value()) << image.error().message;
        ICommandList& commands = device->BeginCommandList(*pool);
        const std::array toPresent{TextureBarrier{*image, ResourceState::Undefined, ResourceState::Present}};
        commands.Barriers({}, toPresent);
        ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device->Submit(lists, **swapchain);
        ASSERT_TRUE((*swapchain)->Present().has_value());
        device->WaitForValue(value);
    }
    device->DestroyCommandPool(*pool);
    swapchain->reset();
}

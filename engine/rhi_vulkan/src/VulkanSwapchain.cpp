// SPDX-License-Identifier: MIT
#include "VulkanSwapchain.h"

#include <interi/core/Assert.h>

#include <algorithm>
#include <format>
#include <mutex>

namespace interi::rhi
{

namespace
{

template <typename To, typename From>
[[nodiscard]] constexpr Handle<To> Retag(Handle<From> handle) noexcept
{
    return Handle<To>::FromParts(handle.Index(), handle.Generation());
}

[[nodiscard]] Format FromVk(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return Format::B8G8R8A8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return Format::B8G8R8A8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return Format::R8G8B8A8Srgb;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return Format::R8G8B8A8Unorm;
    default:
        return Format::Unknown;
    }
}

[[nodiscard]] VkSemaphore CreateBinarySemaphore(const VulkanDevice::Impl& impl)
{
    const VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    INTERI_VK_CHECK(impl.vk.vkCreateSemaphore(impl.device, &info, nullptr, &semaphore));
    return semaphore;
}

} // namespace

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, const SwapchainDesc& desc)
    : m_device(device)
    , m_impl(device.GetImpl())
    , m_surface(surface)
    , m_vsync(desc.vsync)
    , m_requested{desc.width, desc.height}
{
}

VulkanSwapchain::~VulkanSwapchain()
{
    m_impl.vk.vkQueueWaitIdle(m_impl.queue);
    DestroyImages();
    for (const VkSemaphore semaphore : m_renderedSemaphores)
    {
        m_impl.vk.vkDestroySemaphore(m_impl.device, semaphore, nullptr);
    }
    for (const AcquireSlot& slot : m_acquire)
    {
        m_impl.vk.vkDestroySemaphore(m_impl.device, slot.semaphore, nullptr);
    }
    if (m_swapchain != VK_NULL_HANDLE)
    {
        m_impl.vk.vkDestroySwapchainKHR(m_impl.device, m_swapchain, nullptr);
    }
    vkDestroySurfaceKHR(m_impl.instance, m_surface, nullptr);
}

void VulkanSwapchain::DestroyImages()
{
    const std::unique_lock lock(m_impl.resourceMutex);
    for (const TextureHandle image : m_images)
    {
        const TextureData* data = m_impl.textures.Get(Retag<TextureData>(image));
        INTERI_ASSERT(data != nullptr);
        if (data != nullptr)
        {
            m_impl.vk.vkDestroyImageView(m_impl.device, data->view, nullptr);
            m_impl.textures.Remove(Retag<TextureData>(image));
        }
    }
    m_images.clear();
}

Result<void> VulkanSwapchain::Recreate()
{
    if (m_requested.width == 0 || m_requested.height == 0)
    {
        return MakeError(ErrorCode::InvalidArgument, "swapchain size is zero (minimised window)");
    }
    // Every submission that could still use the old images or semaphores finishes first.
    m_impl.vk.vkQueueWaitIdle(m_impl.queue);

    VkBool32 presentable = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_impl.physicalDevice, m_impl.queueFamily, m_surface, &presentable);
    if (presentable != VK_TRUE)
    {
        return MakeError(ErrorCode::Unsupported, "the device's queue cannot present to this surface");
    }
    VkSurfaceCapabilitiesKHR caps{};
    INTERI_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_impl.physicalDevice, m_surface, &caps));
    // A window surface dictates its size; a headless one reports 0xFFFFFFFF and takes the requested size.
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFF'FFFFu)
    {
        extent.width = std::clamp(m_requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(m_requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
    {
        return MakeError(ErrorCode::InvalidArgument, "swapchain size is zero (minimised window)");
    }
    std::uint32_t imageCount = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount != 0)
    {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl.physicalDevice, m_surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_impl.physicalDevice, m_surface, &count, formats.data());
    const auto preferred = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& format)
                                                { return FromVk(format.format) == Format::B8G8R8A8Srgb; });
    const auto supported = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& format)
                                                { return FromVk(format.format) != Format::Unknown; });
    if (supported == formats.end())
    {
        return MakeError(ErrorCode::Unsupported, "no supported swapchain format");
    }
    const VkSurfaceFormatKHR surfaceFormat = preferred != formats.end() ? *preferred : *supported;

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
    if (!m_vsync)
    {
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_impl.physicalDevice, m_surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_impl.physicalDevice, m_surface, &count, modes.data());
        for (const VkPresentModeKHR mode : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
        {
            if (std::ranges::find(modes, mode) != modes.end())
            {
                presentMode = mode;
                break;
            }
        }
    }

    const VkSwapchainKHR old = m_swapchain;
    const VkSwapchainCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                        .surface = m_surface,
                                        .minImageCount = imageCount,
                                        .imageFormat = surfaceFormat.format,
                                        .imageColorSpace = surfaceFormat.colorSpace,
                                        .imageExtent = extent,
                                        .imageArrayLayers = 1,
                                        .imageUsage =
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                        .preTransform = caps.currentTransform,
                                        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                        .presentMode = presentMode,
                                        .clipped = VK_TRUE,
                                        .oldSwapchain = old};
    if (const VkResult result = m_impl.vk.vkCreateSwapchainKHR(m_impl.device, &info, nullptr, &m_swapchain);
        result != VK_SUCCESS)
    {
        m_swapchain = old;
        return MakeError(ErrorCode::Unknown, std::format("vkCreateSwapchainKHR failed: {}", static_cast<int>(result)));
    }
    DestroyImages();
    if (old != VK_NULL_HANDLE)
    {
        m_impl.vk.vkDestroySwapchainKHR(m_impl.device, old, nullptr);
    }
    m_extent = extent;
    m_format = FromVk(surfaceFormat.format);
    m_dirty = false;

    m_impl.vk.vkGetSwapchainImagesKHR(m_impl.device, m_swapchain, &count, nullptr);
    std::vector<VkImage> images(count);
    m_impl.vk.vkGetSwapchainImagesKHR(m_impl.device, m_swapchain, &count, images.data());
    for (const VkImage image : images)
    {
        TextureData data{.image = image,
                         .format = surfaceFormat.format,
                         .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                         .width = extent.width,
                         .height = extent.height,
                         .ownsImage = false};
        const VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                             .image = image,
                                             .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                             .format = surfaceFormat.format,
                                             .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                  .baseMipLevel = 0,
                                                                  .levelCount = 1,
                                                                  .baseArrayLayer = 0,
                                                                  .layerCount = 1}};
        INTERI_VK_CHECK(m_impl.vk.vkCreateImageView(m_impl.device, &viewInfo, nullptr, &data.view));
        const std::unique_lock lock(m_impl.resourceMutex);
        m_images.push_back(Retag<TextureTag>(m_impl.textures.Insert(data)));
    }

    while (m_renderedSemaphores.size() < m_images.size())
    {
        m_renderedSemaphores.push_back(CreateBinarySemaphore(m_impl));
    }
    while (m_acquire.size() < m_images.size() + 1)
    {
        m_acquire.push_back(AcquireSlot{.semaphore = CreateBinarySemaphore(m_impl)});
    }
    return {};
}

Result<TextureHandle> VulkanSwapchain::AcquireNextImage()
{
    INTERI_ASSERT(!m_acquired, "Present the acquired image before acquiring another");
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (m_dirty)
        {
            INTERI_TRY(Recreate());
        }
        AcquireSlot& slot = m_acquire[m_nextAcquire];
        if (slot.lastUse != 0)
        {
            m_device.WaitForValue(slot.lastUse);
        }
        const VkResult result = m_impl.vk.vkAcquireNextImageKHR(m_impl.device, m_swapchain, UINT64_MAX, slot.semaphore,
                                                                VK_NULL_HANDLE, &m_imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_dirty = true;
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            return MakeError(ErrorCode::Unknown,
                             std::format("vkAcquireNextImageKHR failed: {}", static_cast<int>(result)));
        }
        m_dirty = result == VK_SUBOPTIMAL_KHR;
        m_currentAcquire = m_nextAcquire;
        m_nextAcquire = (m_nextAcquire + 1) % static_cast<std::uint32_t>(m_acquire.size());
        m_acquired = true;
        return m_images[m_imageIndex];
    }
    return MakeError(ErrorCode::Unknown, "swapchain stayed out of date after recreation");
}

VulkanSwapchain::SubmitSemaphores VulkanSwapchain::BeginSubmit(std::uint64_t timelineValue)
{
    INTERI_ASSERT(m_acquired, "Submit with a swapchain requires an acquired image");
    m_acquire[m_currentAcquire].lastUse = timelineValue;
    return {m_acquire[m_currentAcquire].semaphore, m_renderedSemaphores[m_imageIndex]};
}

Result<void> VulkanSwapchain::Present()
{
    INTERI_ASSERT(m_acquired, "Present requires an acquired image");
    m_acquired = false;
    const VkPresentInfoKHR info{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &m_renderedSemaphores[m_imageIndex],
                                .swapchainCount = 1,
                                .pSwapchains = &m_swapchain,
                                .pImageIndices = &m_imageIndex};
    const VkResult result = m_impl.vk.vkQueuePresentKHR(m_impl.queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_dirty = true;
        return {};
    }
    if (result != VK_SUCCESS)
    {
        return MakeError(ErrorCode::Unknown, std::format("vkQueuePresentKHR failed: {}", static_cast<int>(result)));
    }
    return {};
}

void VulkanSwapchain::Resize(std::uint32_t width, std::uint32_t height)
{
    if (width != m_requested.width || height != m_requested.height)
    {
        m_requested = {width, height};
        m_dirty = true;
    }
}

} // namespace interi::rhi

// SPDX-License-Identifier: MIT
// Private to rhi_vulkan.
#pragma once

#include <interi/rhi/Swapchain.h>

#include "VulkanImpl.h"

#include <cstdint>
#include <vector>

namespace interi::rhi
{

class VulkanSwapchain final : public ISwapchain
{
public:
    /// Takes ownership of `surface`.
    VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, const SwapchainDesc& desc);
    ~VulkanSwapchain() override;

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    /// Creates the first VkSwapchainKHR; called once by VulkanDevice::CreateSwapchain.
    [[nodiscard]] Result<void> Recreate();

    [[nodiscard]] Result<TextureHandle> AcquireNextImage() override;
    [[nodiscard]] Result<void> Present() override;
    void Resize(std::uint32_t width, std::uint32_t height) override;

    [[nodiscard]] Format ImageFormat() const override { return m_format; }
    [[nodiscard]] std::uint32_t Width() const override { return m_extent.width; }
    [[nodiscard]] std::uint32_t Height() const override { return m_extent.height; }
    [[nodiscard]] std::uint32_t ImageCount() const override { return static_cast<std::uint32_t>(m_images.size()); }

    /// For VulkanDevice::Submit: the semaphore to wait on before writing the acquired image, and the one to signal
    /// for Present. `timelineValue` is the submission's value, used to know when the acquire semaphore is free.
    struct SubmitSemaphores
    {
        VkSemaphore acquired;
        VkSemaphore rendered;
    };
    [[nodiscard]] SubmitSemaphores BeginSubmit(std::uint64_t timelineValue);

private:
    void DestroyImages();

    VulkanDevice& m_device;
    VulkanDevice::Impl& m_impl;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    bool m_vsync;
    VkExtent2D m_requested;
    VkExtent2D m_extent{};
    bool m_dirty = false;
    Format m_format = Format::Unknown;

    std::vector<TextureHandle> m_images;
    std::vector<VkSemaphore> m_renderedSemaphores; ///< One per image: signalled by Submit, waited by Present.

    /// Acquire semaphores are a ring; each records the submission that waited on it, so it is only reused once that
    /// submission completed (only then is the wait, and so the semaphore, guaranteed to be finished).
    struct AcquireSlot
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        std::uint64_t lastUse = 0;
    };
    std::vector<AcquireSlot> m_acquire;
    std::uint32_t m_nextAcquire = 0;
    std::uint32_t m_currentAcquire = 0;
    std::uint32_t m_imageIndex = 0;
    bool m_acquired = false;
};

} // namespace interi::rhi

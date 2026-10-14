// SPDX-License-Identifier: MIT
// Private to rhi_vulkan: the Vulkan state behind VulkanDevice.
#pragma once

#include <interi/core/containers/SlotMap.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include <vk_mem_alloc.h>
#include <volk.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <shared_mutex>
#include <string>

namespace interi::rhi
{

/// Aborts through INTERI_VERIFY when a Vulkan call that cannot fail in a correct program fails.
void CheckVk(VkResult result, const char* call);
#define INTERI_VK_CHECK(call) ::interi::rhi::CheckVk((call), #call)

struct BufferData
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    std::byte* mapped = nullptr;
    std::uint64_t size = 0;
};

struct TextureData
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
};

class VulkanCommandList final : public ICommandList
{
public:
    VulkanCommandList(VulkanDevice::Impl& impl, VkCommandBuffer commandBuffer)
        : m_impl(impl)
        , m_commandBuffer(commandBuffer)
    {
    }

    void CopyBuffer(BufferHandle src, std::uint64_t srcOffset, BufferHandle dst, std::uint64_t dstOffset,
                    std::uint64_t size) override;
    void Barriers(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures) override;

    [[nodiscard]] VkCommandBuffer Native() const noexcept { return m_commandBuffer; }

private:
    VulkanDevice::Impl& m_impl;
    VkCommandBuffer m_commandBuffer;
};

struct CommandPoolData
{
    VkCommandPool pool = VK_NULL_HANDLE;
    std::deque<VulkanCommandList> lists; ///< Allocated command buffers, reused after each reset.
    std::size_t used = 0;
};

struct VulkanDevice::Impl
{
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkDevice device = VK_NULL_HANDLE;
    VolkDeviceTable vk{}; ///< Device-level functions for this device (volk globals are shared by all devices).
    std::uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::uint64_t lastSubmitted = 0;
    bool debugUtils = false;
    std::atomic<std::uint32_t> validationErrors{0};

    mutable std::shared_mutex resourceMutex; ///< Guards the tables below (structure, not the objects).
    SlotMap<BufferData> buffers;
    SlotMap<TextureData> textures;
    SlotMap<std::unique_ptr<CommandPoolData>> pools;

    [[nodiscard]] BufferData Buffer(BufferHandle handle) const;
    [[nodiscard]] TextureData Texture(TextureHandle handle) const;
    void SetName(VkObjectType type, std::uint64_t object, std::string_view name) const;
};

} // namespace interi::rhi

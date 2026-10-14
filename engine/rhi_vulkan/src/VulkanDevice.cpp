// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/rhi_vulkan/ShaderReflection.h>

#include "VulkanImpl.h"
#include "VulkanSwapchain.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <vector>

namespace interi::rhi
{

namespace
{

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

template <typename To, typename From>
[[nodiscard]] constexpr Handle<To> Retag(Handle<From> handle) noexcept
{
    return Handle<To>::FromParts(handle.Index(), handle.Generation());
}

void DestroyViews(const VulkanDevice::Impl& state, const TextureData& data)
{
    state.vk.vkDestroyImageView(state.device, data.view, nullptr);
    for (const VkImageView view : data.layerViews)
    {
        state.vk.vkDestroyImageView(state.device, view, nullptr);
    }
}

/// Loads the Vulkan loader once per process.
[[nodiscard]] bool LoaderAvailable()
{
    static const bool kLoaded = volkInitialize() == VK_SUCCESS && volkGetInstanceVersion() >= VK_API_VERSION_1_3;
    return kLoaded;
}

VKAPI_ATTR VkBool32 VKAPI_CALL OnValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        static_cast<std::atomic<std::uint32_t>*>(user)->fetch_add(1, std::memory_order_relaxed);
        // Also straight to stderr: tests fail on validation errors and must show why, logger or not.
        std::println(stderr, "[Vulkan validation] {}", data->pMessage);
        INTERI_LOG_ERROR("Vulkan", "{}", data->pMessage);
    }
    else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        INTERI_LOG_WARN("Vulkan", "{}", data->pMessage);
    }
    return VK_FALSE;
}

[[nodiscard]] bool HasLayer(const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::ranges::any_of(layers, [name](const VkLayerProperties& layer)
                               { return std::strcmp(layer.layerName, name) == 0; });
}

[[nodiscard]] bool HasInstanceExtension(const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension)
                               { return std::strcmp(extension.extensionName, name) == 0; });
}

[[nodiscard]] bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension)
                               { return std::strcmp(extension.extensionName, name) == 0; });
}

/// The feature chain the engine requires; Query() fills it from a device, Enable() requests exactly the required
/// subset.
struct FeatureChain
{
    VkPhysicalDeviceFeatures2 core{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features v11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

    FeatureChain()
    {
        core.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
    }
    FeatureChain(const FeatureChain&) = delete;
    FeatureChain& operator=(const FeatureChain&) = delete;
    FeatureChain(FeatureChain&&) = delete;
    FeatureChain& operator=(FeatureChain&&) = delete;
    ~FeatureChain() = default;

    void Query(VkPhysicalDevice device) { vkGetPhysicalDeviceFeatures2(device, &core); }

    void Require()
    {
        core.features.multiDrawIndirect = VK_TRUE;
        core.features.drawIndirectFirstInstance = VK_TRUE;
        // Every stage sees the bindless buffer table as read/write storage buffers.
        core.features.vertexPipelineStoresAndAtomics = VK_TRUE;
        core.features.fragmentStoresAndAtomics = VK_TRUE;
        v11.shaderDrawParameters = VK_TRUE;
        v12.timelineSemaphore = VK_TRUE;
        v12.bufferDeviceAddress = VK_TRUE;
        v12.drawIndirectCount = VK_TRUE;
        v12.scalarBlockLayout = VK_TRUE;
        v12.descriptorIndexing = VK_TRUE;
        v12.runtimeDescriptorArray = VK_TRUE;
        v12.descriptorBindingPartiallyBound = VK_TRUE;
        v12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        v12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        v12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        v12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        v12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        v13.dynamicRendering = VK_TRUE;
        v13.synchronization2 = VK_TRUE;
    }

    /// True when every feature Require() asks for is present.
    [[nodiscard]] bool Supports() const
    {
        return core.features.multiDrawIndirect && core.features.drawIndirectFirstInstance &&
               core.features.vertexPipelineStoresAndAtomics && core.features.fragmentStoresAndAtomics &&
               v11.shaderDrawParameters && v12.timelineSemaphore && v12.bufferDeviceAddress && v12.drawIndirectCount &&
               v12.scalarBlockLayout && v12.descriptorIndexing && v12.runtimeDescriptorArray &&
               v12.descriptorBindingPartiallyBound && v12.descriptorBindingUpdateUnusedWhilePending &&
               v12.descriptorBindingSampledImageUpdateAfterBind && v12.descriptorBindingStorageImageUpdateAfterBind &&
               v12.descriptorBindingStorageBufferUpdateAfterBind && v12.shaderSampledImageArrayNonUniformIndexing &&
               v12.shaderStorageBufferArrayNonUniformIndexing && v13.dynamicRendering && v13.synchronization2;
    }
};

[[nodiscard]] std::optional<std::uint32_t> FindQueueFamily(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    constexpr VkQueueFlags kNeeded = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & kNeeded) == kNeeded)
        {
            return i;
        }
    }
    return std::nullopt;
}

/// Lower is better: discrete, integrated, virtual, then CPU (lavapipe) devices.
[[nodiscard]] int DeviceTypeRank(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 3;
    default:
        return 4;
    }
}

[[nodiscard]] VkFormat ToVk(Format format)
{
    switch (format)
    {
    case Format::Unknown:
        return VK_FORMAT_UNDEFINED;
    case Format::R8G8B8A8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::B8G8R8A8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8A8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16G16B16A16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    case Format::R32G32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32G32B32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32B32A32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkCompareOp ToVk(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

[[nodiscard]] VkBufferUsageFlags ToVk(BufferUsage usage)
{
    VkBufferUsageFlags flags = 0;
    const auto add = [&](BufferUsage bit, VkBufferUsageFlags vk)
    {
        if (HasFlags(usage, bit))
        {
            flags |= vk;
        }
    };
    add(BufferUsage::CopySrc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    add(BufferUsage::CopyDst, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    add(BufferUsage::Storage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    add(BufferUsage::Uniform, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    add(BufferUsage::Vertex, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    add(BufferUsage::Index, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    add(BufferUsage::Indirect, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    return flags;
}

[[nodiscard]] VkImageUsageFlags ToVk(TextureUsage usage)
{
    VkImageUsageFlags flags = 0;
    const auto add = [&](TextureUsage bit, VkImageUsageFlags vk)
    {
        if (HasFlags(usage, bit))
        {
            flags |= vk;
        }
    };
    add(TextureUsage::CopySrc, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    add(TextureUsage::CopyDst, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    add(TextureUsage::Sampled, VK_IMAGE_USAGE_SAMPLED_BIT);
    add(TextureUsage::Storage, VK_IMAGE_USAGE_STORAGE_BIT);
    add(TextureUsage::ColorTarget, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    add(TextureUsage::DepthTarget, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    return flags;
}

struct StateInfo
{
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

[[nodiscard]] StateInfo ToVk(ResourceState state)
{
    constexpr VkPipelineStageFlags2 kShaders = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    constexpr VkPipelineStageFlags2 kDepthTests =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    switch (state)
    {
    case ResourceState::Undefined:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::CopySrc:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceState::CopyDst:
        return {VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceState::ShaderRead:
        return {kShaders, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceState::ShaderWrite:
        return {kShaders, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceState::ColorTarget:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ResourceState::DepthWrite:
        return {kDepthTests,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
    case ResourceState::DepthRead:
        return {kDepthTests | kShaders, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
    case ResourceState::VertexBuffer:
        return {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::IndexBuffer:
        return {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::IndirectArgument:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::HostRead:
        return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceState::Present:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL};
}

[[nodiscard]] Error VkError(VkResult result, std::string_view what)
{
    const ErrorCode code = (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
                               ? ErrorCode::OutOfMemory
                               : ErrorCode::Unknown;
    return Error{.code = code, .message = std::format("{} failed: VkResult {}", what, static_cast<int>(result))};
}

} // namespace

void CheckVk(VkResult result, const char* call)
{
    INTERI_VERIFY(result == VK_SUCCESS, call);
}

// --- Impl --------------------------------------------------------------------------------------------------------

BufferData VulkanDevice::Impl::Buffer(BufferHandle handle) const
{
    const std::shared_lock lock(resourceMutex);
    const BufferData* data = buffers.Get(Retag<BufferData>(handle));
    INTERI_ASSERT(data != nullptr, "stale or invalid BufferHandle");
    return data != nullptr ? *data : BufferData{};
}

TextureData VulkanDevice::Impl::Texture(TextureHandle handle) const
{
    const std::shared_lock lock(resourceMutex);
    const TextureData* data = textures.Get(Retag<TextureData>(handle));
    INTERI_ASSERT(data != nullptr, "stale or invalid TextureHandle");
    return data != nullptr ? *data : TextureData{};
}

PipelineData VulkanDevice::Impl::Pipeline(PipelineHandle handle) const
{
    const std::shared_lock lock(resourceMutex);
    const PipelineData* data = pipelines.Get(Retag<PipelineData>(handle));
    INTERI_ASSERT(data != nullptr, "stale or invalid PipelineHandle");
    return data != nullptr ? *data : PipelineData{};
}

void VulkanDevice::Impl::SetName(VkObjectType type, std::uint64_t object, std::string_view name) const
{
    if (!debugUtils || name.empty())
    {
        return;
    }
    const std::string terminated(name);
    const VkDebugUtilsObjectNameInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                                             .objectType = type,
                                             .objectHandle = object,
                                             .pObjectName = terminated.c_str()};
    vkSetDebugUtilsObjectNameEXT(device, &info); // instance-level entry point (volk global)
}

// --- Command list ------------------------------------------------------------------------------------------------

void VulkanCommandList::CopyBuffer(BufferHandle src, std::uint64_t srcOffset, BufferHandle dst, std::uint64_t dstOffset,
                                   std::uint64_t size)
{
    const VkBufferCopy region{.srcOffset = srcOffset, .dstOffset = dstOffset, .size = size};
    m_impl.vk.vkCmdCopyBuffer(m_commandBuffer, m_impl.Buffer(src).buffer, m_impl.Buffer(dst).buffer, 1, &region);
}

void VulkanCommandList::Barriers(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures)
{
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    bufferBarriers.reserve(buffers.size());
    for (const BufferBarrier& barrier : buffers)
    {
        const StateInfo before = ToVk(barrier.before);
        const StateInfo after = ToVk(barrier.after);
        bufferBarriers.push_back(VkBufferMemoryBarrier2{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                                        .srcStageMask = before.stages,
                                                        .srcAccessMask = before.access,
                                                        .dstStageMask = after.stages,
                                                        .dstAccessMask = after.access,
                                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                        .buffer = m_impl.Buffer(barrier.buffer).buffer,
                                                        .offset = 0,
                                                        .size = VK_WHOLE_SIZE});
    }
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    imageBarriers.reserve(textures.size());
    for (const TextureBarrier& barrier : textures)
    {
        const TextureData texture = m_impl.Texture(barrier.texture);
        const StateInfo before = ToVk(barrier.before);
        StateInfo after = ToVk(barrier.after);
        if (barrier.after == ResourceState::ShaderRead && texture.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            after.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        }
        VkImageLayout oldLayout = before.layout;
        if (barrier.before == ResourceState::ShaderRead && texture.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        }
        imageBarriers.push_back(VkImageMemoryBarrier2{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                                      .srcStageMask = before.stages,
                                                      .srcAccessMask = before.access,
                                                      .dstStageMask = after.stages,
                                                      .dstAccessMask = after.access,
                                                      .oldLayout = oldLayout,
                                                      .newLayout = after.layout,
                                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                      .image = texture.image,
                                                      .subresourceRange = {.aspectMask = texture.aspect,
                                                                           .baseMipLevel = 0,
                                                                           .levelCount = VK_REMAINING_MIP_LEVELS,
                                                                           .baseArrayLayer = 0,
                                                                           .layerCount = VK_REMAINING_ARRAY_LAYERS}});
    }
    const VkDependencyInfo dependency{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .bufferMemoryBarrierCount = static_cast<std::uint32_t>(bufferBarriers.size()),
                                      .pBufferMemoryBarriers = bufferBarriers.data(),
                                      .imageMemoryBarrierCount = static_cast<std::uint32_t>(imageBarriers.size()),
                                      .pImageMemoryBarriers = imageBarriers.data()};
    m_impl.vk.vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void VulkanCommandList::FillBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value)
{
    INTERI_ASSERT(offset % 4 == 0 && size % 4 == 0);
    m_impl.vk.vkCmdFillBuffer(m_commandBuffer, m_impl.Buffer(buffer).buffer, offset, size, value);
}

void VulkanCommandList::AliasingBarrier()
{
    const VkMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                                   .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                   .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier};
    m_impl.vk.vkCmdPipelineBarrier2(m_commandBuffer, &dependency);
}

void VulkanCommandList::BindPipeline(PipelineHandle pipeline)
{
    const PipelineData data = m_impl.Pipeline(pipeline);
    m_impl.vk.vkCmdBindPipeline(m_commandBuffer, data.bindPoint, data.pipeline);
    m_impl.vk.vkCmdBindDescriptorSets(m_commandBuffer, data.bindPoint, m_impl.pipelineLayout, 0, 1, &m_impl.bindlessSet,
                                      0, nullptr);
}

void VulkanCommandList::PushConstants(std::span<const std::byte> data)
{
    INTERI_ASSERT(data.size() <= kMaxPushConstantSize && data.size() % 4 == 0);
    m_impl.vk.vkCmdPushConstants(m_commandBuffer, m_impl.pipelineLayout, VK_SHADER_STAGE_ALL, 0,
                                 static_cast<std::uint32_t>(data.size()), data.data());
}

void VulkanCommandList::Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ)
{
    m_impl.vk.vkCmdDispatch(m_commandBuffer, groupsX, groupsY, groupsZ);
}

[[nodiscard]] VkAttachmentLoadOp ToVk(LoadOp op)
{
    switch (op)
    {
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

[[nodiscard]] VkAttachmentStoreOp ToVk(StoreOp op)
{
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

void VulkanCommandList::BeginRendering(const RenderingDesc& desc)
{
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(desc.colors.size());
    for (const ColorAttachment& color : desc.colors)
    {
        VkClearValue clear{};
        clear.color = {{color.clearColor[0], color.clearColor[1], color.clearColor[2], color.clearColor[3]}};
        colors.push_back(VkRenderingAttachmentInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                   .imageView = m_impl.Texture(color.texture).view,
                                                   .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                   .loadOp = ToVk(color.load),
                                                   .storeOp = ToVk(color.store),
                                                   .clearValue = clear});
    }
    VkRenderingAttachmentInfo depth{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (desc.depth != nullptr)
    {
        const TextureData texture = m_impl.Texture(desc.depth->texture);
        INTERI_ASSERT(desc.depth->arrayLayer < texture.arrayLayers);
        depth.imageView = texture.arrayLayers > 1 ? texture.layerViews[desc.depth->arrayLayer] : texture.view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = ToVk(desc.depth->load);
        depth.storeOp = ToVk(desc.depth->store);
        depth.clearValue.depthStencil = {desc.depth->clearDepth, 0};
    }
    const VkRenderingInfo info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                               .renderArea = {{0, 0}, {desc.width, desc.height}},
                               .layerCount = 1,
                               .colorAttachmentCount = static_cast<std::uint32_t>(colors.size()),
                               .pColorAttachments = colors.data(),
                               .pDepthAttachment = desc.depth != nullptr ? &depth : nullptr};
    m_impl.vk.vkCmdBeginRendering(m_commandBuffer, &info);
    SetViewport(0.0f, 0.0f, static_cast<float>(desc.width), static_cast<float>(desc.height));
    SetScissor(0, 0, desc.width, desc.height);
}

void VulkanCommandList::EndRendering()
{
    m_impl.vk.vkCmdEndRendering(m_commandBuffer);
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height)
{
    // Negative height flips Vulkan's y-down clip space so the engine's clip space is +Y up, as in D3D.
    const VkViewport viewport{
        .x = x, .y = y + height, .width = width, .height = -height, .minDepth = 0.0f, .maxDepth = 1.0f};
    m_impl.vk.vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
}

void VulkanCommandList::SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height)
{
    const VkRect2D scissor{{x, y}, {width, height}};
    m_impl.vk.vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format)
{
    m_impl.vk.vkCmdBindIndexBuffer(m_commandBuffer, m_impl.Buffer(buffer).buffer, offset,
                                   format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                             std::uint32_t firstInstance)
{
    m_impl.vk.vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                                    std::int32_t vertexOffset, std::uint32_t firstInstance)
{
    m_impl.vk.vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::DrawIndexedIndirectCount(BufferHandle arguments, std::uint64_t argumentOffset,
                                                 BufferHandle count, std::uint64_t countOffset, std::uint32_t maxDraws,
                                                 std::uint32_t stride)
{
    m_impl.vk.vkCmdDrawIndexedIndirectCount(m_commandBuffer, m_impl.Buffer(arguments).buffer, argumentOffset,
                                            m_impl.Buffer(count).buffer, countOffset, maxDraws, stride);
}

void VulkanCommandList::CopyTextureToBuffer(TextureHandle texture, BufferHandle buffer, std::uint64_t bufferOffset,
                                            std::uint32_t rowPitch)
{
    const TextureData data = m_impl.Texture(texture);
    const std::uint32_t pixelSize = BytesPerPixel(data.rhiFormat);
    INTERI_ASSERT(rowPitch % kTextureRowPitchAlignment == 0 && pixelSize != 0 && rowPitch % pixelSize == 0);
    const VkBufferImageCopy region{
        .bufferOffset = bufferOffset,
        .bufferRowLength = rowPitch / pixelSize,
        .bufferImageHeight = data.height,
        .imageSubresource = {.aspectMask = data.aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {data.width, data.height, 1}};
    m_impl.vk.vkCmdCopyImageToBuffer(m_commandBuffer, data.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     m_impl.Buffer(buffer).buffer, 1, &region);
}

// --- Device ------------------------------------------------------------------------------------------------------

VulkanDevice::VulkanDevice(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

namespace
{

/// The bindless descriptor set (update-after-bind, partially bound) and the pipeline layout shared by every
/// pipeline: that set plus kMaxPushConstantSize bytes of push constants visible to all stages.
Result<void> CreateBindlessTables(VulkanDevice::Impl& state)
{
    VkPhysicalDeviceVulkan12Properties limits{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits};
    vkGetPhysicalDeviceProperties2(state.physicalDevice, &properties);
    constexpr std::uint32_t kMaxBuffers = 16 * 1024;
    constexpr std::uint32_t kMaxTextures = 16 * 1024;
    constexpr std::uint32_t kMaxSamplers = 64;
    const std::uint32_t buffers = std::min({kMaxBuffers, limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
                                            limits.maxDescriptorSetUpdateAfterBindStorageBuffers});
    const std::uint32_t textures = std::min({kMaxTextures, limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
                                             limits.maxDescriptorSetUpdateAfterBindSampledImages});
    const std::uint32_t samplers = std::min({kMaxSamplers, limits.maxPerStageDescriptorUpdateAfterBindSamplers,
                                             limits.maxDescriptorSetUpdateAfterBindSamplers});
    state.bufferSlots = BindlessSlots(buffers);
    state.textureSlots = BindlessSlots(textures);
    state.samplerSlots = BindlessSlots(samplers);

    const std::array bindings{
        VkDescriptorSetLayoutBinding{.binding = kBindlessBuffers,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     .descriptorCount = buffers,
                                     .stageFlags = VK_SHADER_STAGE_ALL},
        VkDescriptorSetLayoutBinding{.binding = kBindlessTextures,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .descriptorCount = textures,
                                     .stageFlags = VK_SHADER_STAGE_ALL},
        VkDescriptorSetLayoutBinding{.binding = kBindlessSamplers,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .descriptorCount = samplers,
                                     .stageFlags = VK_SHADER_STAGE_ALL},
    };
    constexpr VkDescriptorBindingFlags kFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                                                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const std::array<VkDescriptorBindingFlags, 3> bindingFlags{kFlags, kFlags, kFlags};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindingFlags.size()),
        .pBindingFlags = bindingFlags.data()};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                     .pNext = &flagsInfo,
                                                     .flags =
                                                         VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                                                     .bindingCount = static_cast<std::uint32_t>(bindings.size()),
                                                     .pBindings = bindings.data()};
    if (const VkResult result =
            state.vk.vkCreateDescriptorSetLayout(state.device, &layoutInfo, nullptr, &state.bindlessLayout);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateDescriptorSetLayout"));
    }

    const std::array poolSizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffers},
                               VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textures},
                               VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, samplers}};
    const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                              .maxSets = 1,
                                              .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
                                              .pPoolSizes = poolSizes.data()};
    if (const VkResult result = state.vk.vkCreateDescriptorPool(state.device, &poolInfo, nullptr, &state.bindlessPool);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateDescriptorPool"));
    }
    const VkDescriptorSetAllocateInfo allocateInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                   .descriptorPool = state.bindlessPool,
                                                   .descriptorSetCount = 1,
                                                   .pSetLayouts = &state.bindlessLayout};
    if (const VkResult result = state.vk.vkAllocateDescriptorSets(state.device, &allocateInfo, &state.bindlessSet);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkAllocateDescriptorSets"));
    }

    const VkPushConstantRange pushConstants{
        .stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = kMaxPushConstantSize};
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                        .setLayoutCount = 1,
                                                        .pSetLayouts = &state.bindlessLayout,
                                                        .pushConstantRangeCount = 1,
                                                        .pPushConstantRanges = &pushConstants};
    if (const VkResult result =
            state.vk.vkCreatePipelineLayout(state.device, &pipelineLayoutInfo, nullptr, &state.pipelineLayout);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreatePipelineLayout"));
    }
    return {};
}

} // namespace

Result<std::unique_ptr<VulkanDevice>> VulkanDevice::Create(const VulkanDeviceDesc& desc)
{
    if (!LoaderAvailable())
    {
        return MakeError(ErrorCode::Unsupported, "no Vulkan 1.3 loader");
    }
    auto impl = std::make_unique<Impl>();
    // From here on, the VulkanDevice destructor releases whatever was created if a later step fails.
    std::unique_ptr<VulkanDevice> device(new VulkanDevice(std::move(impl)));
    Impl& state = *device->m_impl;

    // Instance.
    std::vector<const char*> extensions(desc.instanceExtensions.begin(), desc.instanceExtensions.end());
    std::vector<const char*> layers;
    state.debugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (state.debugUtils)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const bool validation = desc.validation && state.debugUtils && HasLayer(kValidationLayer);
    if (desc.validation && !validation)
    {
        INTERI_LOG_WARN("Vulkan", "validation requested but {} is not installed", kValidationLayer);
    }
    if (validation)
    {
        layers.push_back(kValidationLayer);
    }
    const VkApplicationInfo application{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                        .pApplicationName = "Interi",
                                        .pEngineName = "Interi",
                                        .apiVersion = VK_API_VERSION_1_3};
    const VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = &OnValidationMessage,
        .pUserData = &state.validationErrors};
    // Synchronization validation is the only check of barrier correctness, so it is on whenever validation is.
    const VkBool32 syncEnabled = VK_TRUE;
    const VkLayerSettingEXT syncValidation{.pLayerName = kValidationLayer,
                                           .pSettingName = "validate_sync",
                                           .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
                                           .valueCount = 1,
                                           .pValues = &syncEnabled};
    const VkLayerSettingsCreateInfoEXT layerSettings{.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
                                                     .pNext = &messengerInfo,
                                                     .settingCount = 1,
                                                     .pSettings = &syncValidation};
    const VkInstanceCreateInfo instanceInfo{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                            .pNext = validation ? &layerSettings : nullptr,
                                            .pApplicationInfo = &application,
                                            .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
                                            .ppEnabledLayerNames = layers.data(),
                                            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
                                            .ppEnabledExtensionNames = extensions.data()};
    if (const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &state.instance); result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateInstance"));
    }
    volkLoadInstanceOnly(state.instance);
    if (validation)
    {
        INTERI_VK_CHECK(vkCreateDebugUtilsMessengerEXT(state.instance, &messengerInfo, nullptr, &state.messenger));
    }

    // Physical device: the best-ranked one with every required feature.
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(state.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> candidates(count);
    vkEnumeratePhysicalDevices(state.instance, &count, candidates.data());
    int bestRank = 1 << 30;
    for (VkPhysicalDevice candidate : candidates)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        FeatureChain features;
        features.Query(candidate);
        const bool suitable = properties.apiVersion >= VK_API_VERSION_1_3 && features.Supports() &&
                              FindQueueFamily(candidate).has_value() &&
                              (!desc.swapchain || HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME));
        if (suitable && DeviceTypeRank(properties.deviceType) < bestRank)
        {
            bestRank = DeviceTypeRank(properties.deviceType);
            state.physicalDevice = candidate;
            state.properties = properties;
        }
    }
    const std::optional<std::uint32_t> queueFamily =
        state.physicalDevice != VK_NULL_HANDLE ? FindQueueFamily(state.physicalDevice) : std::nullopt;
    if (!queueFamily.has_value())
    {
        return MakeError(ErrorCode::Unsupported, "no Vulkan 1.3 device with the required features");
    }
    state.queueFamily = *queueFamily;

    // Logical device.
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                            .queueFamilyIndex = state.queueFamily,
                                            .queueCount = 1,
                                            .pQueuePriorities = &priority};
    FeatureChain enabled;
    enabled.Require();
    std::vector<const char*> deviceExtensions;
    if (desc.swapchain)
    {
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    const VkDeviceCreateInfo deviceInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                        .pNext = &enabled.core,
                                        .queueCreateInfoCount = 1,
                                        .pQueueCreateInfos = &queueInfo,
                                        .enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size()),
                                        .ppEnabledExtensionNames = deviceExtensions.data()};
    if (const VkResult result = vkCreateDevice(state.physicalDevice, &deviceInfo, nullptr, &state.device);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateDevice"));
    }
    volkLoadDeviceTable(&state.vk, state.device);
    state.vk.vkGetDeviceQueue(state.device, state.queueFamily, 0, &state.queue);

    // Memory allocator.
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    const VmaAllocatorCreateInfo allocatorInfo{.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
                                               .physicalDevice = state.physicalDevice,
                                               .device = state.device,
                                               .pVulkanFunctions = &functions,
                                               .instance = state.instance,
                                               .vulkanApiVersion = VK_API_VERSION_1_3};
    if (const VkResult result = vmaCreateAllocator(&allocatorInfo, &state.allocator); result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaCreateAllocator"));
    }

    // Queue timeline.
    const VkSemaphoreTypeCreateInfo timelineType{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                 .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                                 .initialValue = 0};
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineType};
    INTERI_VK_CHECK(state.vk.vkCreateSemaphore(state.device, &semaphoreInfo, nullptr, &state.timeline));

    INTERI_TRY(CreateBindlessTables(state));

    INTERI_LOG_INFO("Vulkan", "device: {}", state.properties.deviceName);
    return device;
}

VulkanDevice::~VulkanDevice()
{
    Impl& state = *m_impl;
    if (state.device != VK_NULL_HANDLE)
    {
        state.vk.vkDeviceWaitIdle(state.device);
        // Anything still alive is a leak in the caller; release it so the validation layer stays quiet about it.
        INTERI_ASSERT(state.buffers.Empty() && state.textures.Empty() && state.pools.Empty() &&
                          state.pipelines.Empty() && state.heaps.Empty() && state.samplers.Empty(),
                      "VulkanDevice destroyed while it still owns resources");
        for (const BufferData& buffer : state.buffers)
        {
            vmaDestroyBuffer(state.allocator, buffer.buffer, buffer.allocation);
        }
        for (const TextureData& texture : state.textures)
        {
            DestroyViews(state, texture);
            if (texture.ownsImage)
            {
                vmaDestroyImage(state.allocator, texture.image, texture.allocation);
            }
        }
        for (const auto& pool : state.pools)
        {
            state.vk.vkDestroyCommandPool(state.device, pool->pool, nullptr);
        }
        for (const SamplerData& sampler : state.samplers)
        {
            state.vk.vkDestroySampler(state.device, sampler.sampler, nullptr);
        }
        for (const HeapData& heap : state.heaps)
        {
            vmaFreeMemory(state.allocator, heap.allocation);
        }
        for (const PipelineData& pipeline : state.pipelines)
        {
            state.vk.vkDestroyPipeline(state.device, pipeline.pipeline, nullptr);
        }
        state.vk.vkDestroyPipelineLayout(state.device, state.pipelineLayout, nullptr);
        state.vk.vkDestroyDescriptorPool(state.device, state.bindlessPool, nullptr);
        state.vk.vkDestroyDescriptorSetLayout(state.device, state.bindlessLayout, nullptr);
        state.vk.vkDestroySemaphore(state.device, state.timeline, nullptr);
        if (state.allocator != nullptr)
        {
            vmaDestroyAllocator(state.allocator);
        }
        state.vk.vkDestroyDevice(state.device, nullptr);
    }
    if (state.messenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(state.instance, state.messenger, nullptr);
    }
    if (state.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(state.instance, nullptr);
    }
}

namespace
{

/// Heaps are allocated with this alignment so any offset aligned for a placed resource is aligned in memory too.
constexpr VkDeviceSize kHeapAlignment = VkDeviceSize{64} * 1024;

[[nodiscard]] VkBufferCreateInfo BufferInfo(const BufferDesc& desc)
{
    INTERI_ASSERT(desc.size > 0);
    return {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.size,
            .usage = ToVk(desc.usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
}

[[nodiscard]] VkImageCreateInfo ImageInfo(const TextureDesc& desc)
{
    INTERI_ASSERT(desc.format != Format::Unknown && desc.width > 0 && desc.height > 0);
    return {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = ToVk(desc.format),
            .extent = {desc.width, desc.height, 1},
            .mipLevels = desc.mipLevels,
            .arrayLayers = desc.arrayLayers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = ToVk(desc.usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
}

/// Names a created buffer, gives it a bindless slot if it is a storage buffer, and registers it.
Result<BufferHandle> RegisterBuffer(VulkanDevice::Impl& state, BufferData data, const BufferDesc& desc)
{
    state.SetName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(data.buffer), desc.debugName);
    const std::unique_lock lock(state.resourceMutex);
    if (HasFlags(desc.usage, BufferUsage::Storage))
    {
        data.bindless = state.bufferSlots.Allocate();
        if (data.bindless == kInvalidBindlessIndex)
        {
            vmaDestroyBuffer(state.allocator, data.buffer, data.allocation);
            return MakeError(ErrorCode::OutOfMemory, "bindless buffer table is full");
        }
        const VkDescriptorBufferInfo info{.buffer = data.buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                         .dstSet = state.bindlessSet,
                                         .dstBinding = kBindlessBuffers,
                                         .dstArrayElement = data.bindless,
                                         .descriptorCount = 1,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         .pBufferInfo = &info};
        state.vk.vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);
    }
    return Retag<BufferTag>(state.buffers.Insert(data));
}

/// Creates the view of a created image, names it, gives it a bindless slot if sampled, and registers it.
Result<TextureHandle> RegisterTexture(VulkanDevice::Impl& state, TextureData data, const TextureDesc& desc)
{
    data.aspect = desc.format == Format::D32Float ? VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT}
                                                  : VkImageAspectFlags{VK_IMAGE_ASPECT_COLOR_BIT};
    data.format = ToVk(desc.format);
    data.rhiFormat = desc.format;
    data.width = desc.width;
    data.height = desc.height;
    data.mipLevels = desc.mipLevels;
    data.arrayLayers = desc.arrayLayers;
    const VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                         .image = data.image,
                                         .viewType =
                                             desc.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
                                         .format = data.format,
                                         .subresourceRange = {.aspectMask = data.aspect,
                                                              .baseMipLevel = 0,
                                                              .levelCount = desc.mipLevels,
                                                              .baseArrayLayer = 0,
                                                              .layerCount = desc.arrayLayers}};
    if (const VkResult result = state.vk.vkCreateImageView(state.device, &viewInfo, nullptr, &data.view);
        result != VK_SUCCESS)
    {
        vmaDestroyImage(state.allocator, data.image, data.allocation);
        return std::unexpected(VkError(result, "vkCreateImageView"));
    }
    if (desc.arrayLayers > 1 &&
        (HasFlags(desc.usage, TextureUsage::ColorTarget) || HasFlags(desc.usage, TextureUsage::DepthTarget)))
    {
        for (std::uint32_t layer = 0; layer < desc.arrayLayers; ++layer)
        {
            VkImageViewCreateInfo layerInfo = viewInfo;
            layerInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            layerInfo.subresourceRange.baseArrayLayer = layer;
            layerInfo.subresourceRange.layerCount = 1;
            VkImageView view = VK_NULL_HANDLE;
            INTERI_VK_CHECK(state.vk.vkCreateImageView(state.device, &layerInfo, nullptr, &view));
            data.layerViews.push_back(view);
        }
    }
    state.SetName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(data.image), desc.debugName);

    const std::unique_lock lock(state.resourceMutex);
    if (HasFlags(desc.usage, TextureUsage::Sampled))
    {
        data.bindless = state.textureSlots.Allocate();
        if (data.bindless == kInvalidBindlessIndex)
        {
            DestroyViews(state, data);
            vmaDestroyImage(state.allocator, data.image, data.allocation);
            return MakeError(ErrorCode::OutOfMemory, "bindless texture table is full");
        }
        const VkDescriptorImageInfo info{.imageView = data.view,
                                         .imageLayout = data.aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                                                            ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
                                                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                         .dstSet = state.bindlessSet,
                                         .dstBinding = kBindlessTextures,
                                         .dstArrayElement = data.bindless,
                                         .descriptorCount = 1,
                                         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                         .pImageInfo = &info};
        state.vk.vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);
    }
    return Retag<TextureTag>(state.textures.Insert(data));
}

[[nodiscard]] VmaAllocation HeapAllocation(const VulkanDevice::Impl& state, HeapHandle heap)
{
    const std::shared_lock lock(state.resourceMutex);
    const HeapData* data = state.heaps.Get(Retag<HeapData>(heap));
    INTERI_ASSERT(data != nullptr, "stale or invalid HeapHandle");
    return data != nullptr ? data->allocation : nullptr;
}

} // namespace

Result<BufferHandle> VulkanDevice::CreateBuffer(const BufferDesc& desc)
{
    Impl& state = *m_impl;
    const VkBufferCreateInfo bufferInfo = BufferInfo(desc);
    VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    switch (desc.memory)
    {
    case MemoryUsage::GpuOnly:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryUsage::Upload:
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryUsage::Readback:
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }

    BufferData data{.size = desc.size};
    VmaAllocationInfo allocated{};
    if (const VkResult result =
            vmaCreateBuffer(state.allocator, &bufferInfo, &allocationInfo, &data.buffer, &data.allocation, &allocated);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaCreateBuffer"));
    }
    data.mapped = static_cast<std::byte*>(allocated.pMappedData);
    return RegisterBuffer(state, data, desc);
}

Result<BufferHandle> VulkanDevice::CreatePlacedBuffer(const BufferDesc& desc, HeapHandle heap, std::uint64_t offset)
{
    INTERI_ASSERT(desc.memory == MemoryUsage::GpuOnly, "placed buffers live in GPU-only heaps");
    Impl& state = *m_impl;
    const VkBufferCreateInfo bufferInfo = BufferInfo(desc);
    BufferData data{.size = desc.size};
    if (const VkResult result =
            vmaCreateAliasingBuffer2(state.allocator, HeapAllocation(state, heap), offset, &bufferInfo, &data.buffer);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaCreateAliasingBuffer2"));
    }
    return RegisterBuffer(state, data, desc);
}

void VulkanDevice::DestroyBuffer(BufferHandle buffer)
{
    Impl& state = *m_impl;
    BufferData data;
    {
        const std::unique_lock lock(state.resourceMutex);
        const BufferData* found = state.buffers.Get(Retag<BufferData>(buffer));
        INTERI_ASSERT(found != nullptr, "stale or invalid BufferHandle");
        if (found == nullptr)
        {
            return;
        }
        data = *found;
        state.buffers.Remove(Retag<BufferData>(buffer));
        state.bufferSlots.Release(data.bindless);
    }
    vmaDestroyBuffer(state.allocator, data.buffer, data.allocation);
}

Result<TextureHandle> VulkanDevice::CreateTexture(const TextureDesc& desc)
{
    Impl& state = *m_impl;
    const VkImageCreateInfo imageInfo = ImageInfo(desc);
    const VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    TextureData data;
    if (const VkResult result =
            vmaCreateImage(state.allocator, &imageInfo, &allocationInfo, &data.image, &data.allocation, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaCreateImage"));
    }
    return RegisterTexture(state, data, desc);
}

Result<TextureHandle> VulkanDevice::CreatePlacedTexture(const TextureDesc& desc, HeapHandle heap, std::uint64_t offset)
{
    Impl& state = *m_impl;
    const VkImageCreateInfo imageInfo = ImageInfo(desc);
    TextureData data;
    if (const VkResult result =
            vmaCreateAliasingImage2(state.allocator, HeapAllocation(state, heap), offset, &imageInfo, &data.image);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaCreateAliasingImage2"));
    }
    return RegisterTexture(state, data, desc);
}

MemoryRequirements VulkanDevice::GetTextureMemoryRequirements(const TextureDesc& desc) const
{
    const VkImageCreateInfo imageInfo = ImageInfo(desc);
    const VkDeviceImageMemoryRequirements query{.sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
                                                .pCreateInfo = &imageInfo};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    m_impl->vk.vkGetDeviceImageMemoryRequirements(m_impl->device, &query, &requirements);
    return {requirements.memoryRequirements.size, requirements.memoryRequirements.alignment,
            requirements.memoryRequirements.memoryTypeBits};
}

MemoryRequirements VulkanDevice::GetBufferMemoryRequirements(const BufferDesc& desc) const
{
    const VkBufferCreateInfo bufferInfo = BufferInfo(desc);
    const VkDeviceBufferMemoryRequirements query{.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
                                                 .pCreateInfo = &bufferInfo};
    VkMemoryRequirements2 requirements{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    m_impl->vk.vkGetDeviceBufferMemoryRequirements(m_impl->device, &query, &requirements);
    return {requirements.memoryRequirements.size, requirements.memoryRequirements.alignment,
            requirements.memoryRequirements.memoryTypeBits};
}

Result<HeapHandle> VulkanDevice::CreateHeap(const HeapDesc& desc)
{
    Impl& state = *m_impl;
    INTERI_ASSERT(desc.size > 0 && desc.memoryTypeBits != 0);
    const VkMemoryRequirements requirements{
        .size = desc.size, .alignment = kHeapAlignment, .memoryTypeBits = desc.memoryTypeBits};
    const VmaAllocationCreateInfo allocationInfo{.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    HeapData data;
    if (const VkResult result =
            vmaAllocateMemory(state.allocator, &requirements, &allocationInfo, &data.allocation, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vmaAllocateMemory"));
    }
    const std::unique_lock lock(state.resourceMutex);
    return Retag<HeapTag>(state.heaps.Insert(data));
}

void VulkanDevice::DestroyHeap(HeapHandle heap)
{
    Impl& state = *m_impl;
    HeapData data;
    {
        const std::unique_lock lock(state.resourceMutex);
        const HeapData* found = state.heaps.Get(Retag<HeapData>(heap));
        INTERI_ASSERT(found != nullptr, "stale or invalid HeapHandle");
        if (found == nullptr)
        {
            return;
        }
        data = *found;
        state.heaps.Remove(Retag<HeapData>(heap));
    }
    vmaFreeMemory(state.allocator, data.allocation);
}

void VulkanDevice::DestroyTexture(TextureHandle texture)
{
    Impl& state = *m_impl;
    TextureData data;
    {
        const std::unique_lock lock(state.resourceMutex);
        const TextureData* found = state.textures.Get(Retag<TextureData>(texture));
        INTERI_ASSERT(found != nullptr, "stale or invalid TextureHandle");
        INTERI_ASSERT(found == nullptr || found->ownsImage, "swapchain images are destroyed by their swapchain");
        if (found == nullptr || !found->ownsImage)
        {
            return;
        }
        data = *found;
        state.textures.Remove(Retag<TextureData>(texture));
        state.textureSlots.Release(data.bindless);
    }
    DestroyViews(state, data);
    vmaDestroyImage(state.allocator, data.image, data.allocation);
}

std::span<std::byte> VulkanDevice::MappedMemory(BufferHandle buffer)
{
    const BufferData data = m_impl->Buffer(buffer);
    if (data.mapped == nullptr)
    {
        return {};
    }
    return {data.mapped, static_cast<std::size_t>(data.size)};
}

std::uint32_t VulkanDevice::BindlessIndex(BufferHandle buffer) const
{
    return m_impl->Buffer(buffer).bindless;
}

std::uint32_t VulkanDevice::BindlessIndex(TextureHandle texture) const
{
    return m_impl->Texture(texture).bindless;
}

std::uint32_t VulkanDevice::BindlessIndex(SamplerHandle sampler) const
{
    const std::shared_lock lock(m_impl->resourceMutex);
    const SamplerData* data = m_impl->samplers.Get(Retag<SamplerData>(sampler));
    INTERI_ASSERT(data != nullptr, "stale or invalid SamplerHandle");
    return data != nullptr ? data->bindless : kInvalidBindlessIndex;
}

Result<SamplerHandle> VulkanDevice::CreateSampler(const SamplerDesc& desc)
{
    Impl& state = *m_impl;
    const VkFilter filter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkSamplerAddressMode address =
        desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const VkSamplerCreateInfo info{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = filter,
                                   .minFilter = filter,
                                   .mipmapMode = desc.filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                               : VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = address,
                                   .addressModeV = address,
                                   .addressModeW = address,
                                   .compareEnable = desc.compare ? VK_TRUE : VK_FALSE,
                                   .compareOp = ToVk(desc.compareOp),
                                   .maxLod = VK_LOD_CLAMP_NONE};
    SamplerData data;
    if (const VkResult result = state.vk.vkCreateSampler(state.device, &info, nullptr, &data.sampler);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateSampler"));
    }
    const std::unique_lock lock(state.resourceMutex);
    data.bindless = state.samplerSlots.Allocate();
    if (data.bindless == kInvalidBindlessIndex)
    {
        state.vk.vkDestroySampler(state.device, data.sampler, nullptr);
        return MakeError(ErrorCode::OutOfMemory, "bindless sampler table is full");
    }
    const VkDescriptorImageInfo image{.sampler = data.sampler};
    const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstSet = state.bindlessSet,
                                     .dstBinding = kBindlessSamplers,
                                     .dstArrayElement = data.bindless,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .pImageInfo = &image};
    state.vk.vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);
    return Retag<SamplerTag>(state.samplers.Insert(data));
}

void VulkanDevice::DestroySampler(SamplerHandle sampler)
{
    Impl& state = *m_impl;
    SamplerData data;
    {
        const std::unique_lock lock(state.resourceMutex);
        const SamplerData* found = state.samplers.Get(Retag<SamplerData>(sampler));
        INTERI_ASSERT(found != nullptr, "stale or invalid SamplerHandle");
        if (found == nullptr)
        {
            return;
        }
        data = *found;
        state.samplers.Remove(Retag<SamplerData>(sampler));
        state.samplerSlots.Release(data.bindless);
    }
    state.vk.vkDestroySampler(state.device, data.sampler, nullptr);
}

Result<PipelineHandle> VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    Impl& state = *m_impl;
    INTERI_TRY_ASSIGN(reflection, ReflectSpirv(desc.code));
    if (reflection.stage != ShaderStage::Compute)
    {
        return MakeError(ErrorCode::InvalidArgument, "CreateComputePipeline needs a compute shader");
    }
    INTERI_ASSERT(reinterpret_cast<std::uintptr_t>(desc.code.data()) % alignof(std::uint32_t) == 0,
                  "SPIR-V must be 4-byte aligned");
    const VkShaderModuleCreateInfo moduleInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                              .codeSize = desc.code.size(),
                                              .pCode = reinterpret_cast<const std::uint32_t*>(desc.code.data())};
    VkShaderModule module = VK_NULL_HANDLE;
    if (const VkResult result = state.vk.vkCreateShaderModule(state.device, &moduleInfo, nullptr, &module);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateShaderModule"));
    }
    const VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = module,
                  .pName = "main"},
        .layout = state.pipelineLayout};
    PipelineData data{.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE};
    const VkResult result =
        state.vk.vkCreateComputePipelines(state.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &data.pipeline);
    state.vk.vkDestroyShaderModule(state.device, module, nullptr);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateComputePipelines"));
    }
    state.SetName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(data.pipeline), desc.debugName);

    const std::unique_lock lock(state.resourceMutex);
    return Retag<PipelineTag>(state.pipelines.Insert(data));
}

Result<PipelineHandle> VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    Impl& state = *m_impl;
    INTERI_TRY_ASSIGN(vertex, ReflectSpirv(desc.vertexCode));
    INTERI_TRY_ASSIGN(pixel, ReflectSpirv(desc.pixelCode));
    if (vertex.stage != ShaderStage::Vertex || pixel.stage != ShaderStage::Pixel)
    {
        return MakeError(ErrorCode::InvalidArgument, "CreateGraphicsPipeline needs a vertex and a pixel shader");
    }
    std::array<VkShaderModule, 2> modules{};
    const std::array<std::span<const std::byte>, 2> code{desc.vertexCode, desc.pixelCode};
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        const VkShaderModuleCreateInfo moduleInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                  .codeSize = code[i].size(),
                                                  .pCode = reinterpret_cast<const std::uint32_t*>(code[i].data())};
        if (const VkResult result = state.vk.vkCreateShaderModule(state.device, &moduleInfo, nullptr, &modules[i]);
            result != VK_SUCCESS)
        {
            for (const VkShaderModule module : modules)
            {
                state.vk.vkDestroyShaderModule(state.device, module, nullptr);
            }
            return std::unexpected(VkError(result, "vkCreateShaderModule"));
        }
    }
    const std::array stages{
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = modules[0],
                                        .pName = "main"},
        VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = modules[1],
                                        .pName = "main"}};

    std::vector<VkFormat> colorFormats;
    for (const Format format : desc.colorFormats)
    {
        colorFormats.push_back(ToVk(format));
    }
    const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                  .colorAttachmentCount =
                                                      static_cast<std::uint32_t>(colorFormats.size()),
                                                  .pColorAttachmentFormats = colorFormats.data(),
                                                  .depthAttachmentFormat = ToVk(desc.depthFormat)};
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    if (desc.cullMode == CullMode::Back)
    {
        cull = VK_CULL_MODE_BACK_BIT;
    }
    else if (desc.cullMode == CullMode::Front)
    {
        cull = VK_CULL_MODE_FRONT_BIT;
    }
    const bool depthBias = desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f;
    // With the y-flipped viewport, VK_FRONT_FACE_COUNTER_CLOCKWISE makes triangles that are counter-clockwise in the
    // engine's +Y-up clip space front-facing (pinned by tests/render/gpu/ConventionTests.cpp).
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cull,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = depthBias ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = desc.depthBiasConstant,
        .depthBiasSlopeFactor = desc.depthBiasSlope,
        .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo multisample{.sType =
                                                               VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = ToVk(desc.depthCompare)};
    const std::vector<VkPipelineColorBlendAttachmentState> blends(
        colorFormats.size(),
        VkPipelineColorBlendAttachmentState{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT});
    const VkPipelineColorBlendStateCreateInfo blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                    .attachmentCount = static_cast<std::uint32_t>(blends.size()),
                                                    .pAttachments = blends.data()};
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                   .dynamicStateCount =
                                                       static_cast<std::uint32_t>(dynamicStates.size()),
                                                   .pDynamicStates = dynamicStates.data()};
    const VkGraphicsPipelineCreateInfo pipelineInfo{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                    .pNext = &rendering,
                                                    .stageCount = static_cast<std::uint32_t>(stages.size()),
                                                    .pStages = stages.data(),
                                                    .pVertexInputState = &vertexInput,
                                                    .pInputAssemblyState = &inputAssembly,
                                                    .pViewportState = &viewport,
                                                    .pRasterizationState = &rasterization,
                                                    .pMultisampleState = &multisample,
                                                    .pDepthStencilState = &depthStencil,
                                                    .pColorBlendState = &blend,
                                                    .pDynamicState = &dynamic,
                                                    .layout = state.pipelineLayout};
    PipelineData data{.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
    const VkResult result =
        state.vk.vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &data.pipeline);
    for (const VkShaderModule module : modules)
    {
        state.vk.vkDestroyShaderModule(state.device, module, nullptr);
    }
    if (result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateGraphicsPipelines"));
    }
    state.SetName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(data.pipeline), desc.debugName);

    const std::unique_lock lock(state.resourceMutex);
    return Retag<PipelineTag>(state.pipelines.Insert(data));
}

void VulkanDevice::DestroyPipeline(PipelineHandle pipeline)
{
    Impl& state = *m_impl;
    PipelineData data;
    {
        const std::unique_lock lock(state.resourceMutex);
        const PipelineData* found = state.pipelines.Get(Retag<PipelineData>(pipeline));
        INTERI_ASSERT(found != nullptr, "stale or invalid PipelineHandle");
        if (found == nullptr)
        {
            return;
        }
        data = *found;
        state.pipelines.Remove(Retag<PipelineData>(pipeline));
    }
    state.vk.vkDestroyPipeline(state.device, data.pipeline, nullptr);
}

Result<CommandPoolHandle> VulkanDevice::CreateCommandPool()
{
    Impl& state = *m_impl;
    auto pool = std::make_unique<CommandPoolData>();
    const VkCommandPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                       .queueFamilyIndex = state.queueFamily};
    if (const VkResult result = state.vk.vkCreateCommandPool(state.device, &info, nullptr, &pool->pool);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateCommandPool"));
    }
    const std::unique_lock lock(state.resourceMutex);
    return Retag<CommandPoolTag>(state.pools.Insert(std::move(pool)));
}

void VulkanDevice::DestroyCommandPool(CommandPoolHandle pool)
{
    Impl& state = *m_impl;
    std::unique_ptr<CommandPoolData> data;
    {
        const std::unique_lock lock(state.resourceMutex);
        auto* found = state.pools.Get(Retag<std::unique_ptr<CommandPoolData>>(pool));
        INTERI_ASSERT(found != nullptr, "stale or invalid CommandPoolHandle");
        if (found == nullptr)
        {
            return;
        }
        data = std::move(*found);
        state.pools.Remove(Retag<std::unique_ptr<CommandPoolData>>(pool));
    }
    state.vk.vkDestroyCommandPool(state.device, data->pool, nullptr);
}

namespace
{

[[nodiscard]] CommandPoolData& FindPool(VulkanDevice::Impl& state, CommandPoolHandle pool)
{
    const std::shared_lock lock(state.resourceMutex);
    auto* found = state.pools.Get(Retag<std::unique_ptr<CommandPoolData>>(pool));
    INTERI_VERIFY(found != nullptr, "stale or invalid CommandPoolHandle");
    return **found;
}

} // namespace

void VulkanDevice::ResetCommandPool(CommandPoolHandle pool)
{
    CommandPoolData& data = FindPool(*m_impl, pool);
    INTERI_VK_CHECK(m_impl->vk.vkResetCommandPool(m_impl->device, data.pool, 0));
    data.used = 0;
}

ICommandList& VulkanDevice::BeginCommandList(CommandPoolHandle pool)
{
    Impl& state = *m_impl;
    CommandPoolData& data = FindPool(state, pool);
    if (data.used == data.lists.size())
    {
        const VkCommandBufferAllocateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                               .commandPool = data.pool,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                               .commandBufferCount = 1};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        INTERI_VK_CHECK(state.vk.vkAllocateCommandBuffers(state.device, &info, &commandBuffer));
        data.lists.emplace_back(state, commandBuffer);
    }
    VulkanCommandList& list = data.lists[data.used++];
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    INTERI_VK_CHECK(state.vk.vkBeginCommandBuffer(list.Native(), &begin));
    return list;
}

std::uint64_t VulkanDevice::Submit(std::span<ICommandList* const> commandLists)
{
    return SubmitImpl(commandLists, nullptr);
}

std::uint64_t VulkanDevice::Submit(std::span<ICommandList* const> commandLists, ISwapchain& swapchain)
{
    return SubmitImpl(commandLists, &swapchain);
}

std::uint64_t VulkanDevice::SubmitImpl(std::span<ICommandList* const> commandLists, ISwapchain* swapchain)
{
    Impl& state = *m_impl;
    std::vector<VkCommandBufferSubmitInfo> buffers;
    buffers.reserve(commandLists.size());
    for (ICommandList* list : commandLists)
    {
        const VkCommandBuffer native = static_cast<VulkanCommandList*>(list)->Native();
        INTERI_VK_CHECK(state.vk.vkEndCommandBuffer(native));
        buffers.push_back(
            VkCommandBufferSubmitInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = native});
    }
    const std::uint64_t value = ++state.lastSubmitted;
    std::array<VkSemaphoreSubmitInfo, 2> signals{
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              .semaphore = state.timeline,
                              .value = value,
                              .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
    VkSemaphoreSubmitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    std::uint32_t signalCount = 1;
    std::uint32_t waitCount = 0;
    if (swapchain != nullptr)
    {
        // Writes to the image wait for the presentation engine to release it; Present waits for this submission.
        const auto semaphores = static_cast<VulkanSwapchain*>(swapchain)->BeginSubmit(value);
        wait.semaphore = semaphores.acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitCount = 1;
        signals[1] = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                           .semaphore = semaphores.rendered,
                                           .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        signalCount = 2;
    }
    const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                               .waitSemaphoreInfoCount = waitCount,
                               .pWaitSemaphoreInfos = &wait,
                               .commandBufferInfoCount = static_cast<std::uint32_t>(buffers.size()),
                               .pCommandBufferInfos = buffers.data(),
                               .signalSemaphoreInfoCount = signalCount,
                               .pSignalSemaphoreInfos = signals.data()};
    INTERI_VK_CHECK(state.vk.vkQueueSubmit2(state.queue, 1, &submit, VK_NULL_HANDLE));
    return value;
}

void* VulkanDevice::NativeInstance() const noexcept
{
    return m_impl->instance;
}

Result<std::uint64_t> VulkanDevice::CreateHeadlessSurface()
{
    if (vkCreateHeadlessSurfaceEXT == nullptr)
    {
        return MakeError(ErrorCode::Unsupported, "VK_EXT_headless_surface was not enabled on the instance");
    }
    const VkHeadlessSurfaceCreateInfoEXT info{.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateHeadlessSurfaceEXT(m_impl->instance, &info, nullptr, &surface);
        result != VK_SUCCESS)
    {
        return std::unexpected(VkError(result, "vkCreateHeadlessSurfaceEXT"));
    }
    return reinterpret_cast<std::uint64_t>(surface);
}

Result<std::unique_ptr<ISwapchain>> VulkanDevice::CreateSwapchain(std::uint64_t surface, const SwapchainDesc& desc)
{
    // NOLINTNEXTLINE(performance-no-int-to-ptr): VkSurfaceKHR is a pointer-sized opaque handle
    auto swapchain = std::make_unique<VulkanSwapchain>(*this, reinterpret_cast<VkSurfaceKHR>(surface), desc);
    INTERI_TRY(swapchain->Recreate());
    return std::unique_ptr<ISwapchain>(std::move(swapchain));
}

std::uint64_t VulkanDevice::LastSubmittedValue() const
{
    return m_impl->lastSubmitted;
}

std::uint64_t VulkanDevice::CompletedValue() const
{
    std::uint64_t value = 0;
    INTERI_VK_CHECK(m_impl->vk.vkGetSemaphoreCounterValue(m_impl->device, m_impl->timeline, &value));
    return value;
}

void VulkanDevice::WaitForValue(std::uint64_t value)
{
    const VkSemaphoreWaitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                   .semaphoreCount = 1,
                                   .pSemaphores = &m_impl->timeline,
                                   .pValues = &value};
    INTERI_VK_CHECK(m_impl->vk.vkWaitSemaphores(m_impl->device, &wait, UINT64_MAX));
}

std::uint32_t VulkanDevice::ValidationErrorCount() const noexcept
{
    return m_impl->validationErrors.load(std::memory_order_relaxed);
}

std::string_view VulkanDevice::DeviceName() const noexcept
{
    return m_impl->properties.deviceName;
}

} // namespace interi::rhi

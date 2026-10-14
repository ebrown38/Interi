// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/Swapchain.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace interi::rhi
{

struct VulkanDeviceDesc
{
    /// Enables VK_LAYER_KHRONOS_validation when it is installed. Messages are logged; errors are counted.
    bool validation = false;
    /// Instance extensions the caller needs on top of the backend's own (e.g. window-system surface extensions).
    std::span<const char* const> instanceExtensions{}; // NOLINT(readability-redundant-member-init)
    /// Enables VK_KHR_swapchain for presenting to windows.
    bool swapchain = false;
};

/// IDevice on Vulkan 1.3 (dynamic rendering, synchronization2, timeline semaphores, descriptor indexing, buffer
/// device address), with memory from the Vulkan Memory Allocator. The Vulkan loader is loaded at runtime (volk),
/// so a machine without Vulkan gets an error from Create rather than a missing-library failure at startup.
///
/// One graphics+compute queue; its timeline semaphore implements the IDevice timeline.
class VulkanDevice final : public IDevice
{
public:
    /// Fails with ErrorCode::Unsupported when there is no Vulkan loader or no device with the required features.
    [[nodiscard]] static Result<std::unique_ptr<VulkanDevice>> Create(const VulkanDeviceDesc& desc);
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    [[nodiscard]] Result<BufferHandle> CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle buffer) override;
    [[nodiscard]] Result<TextureHandle> CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle texture) override;
    [[nodiscard]] std::span<std::byte> MappedMemory(BufferHandle buffer) override;
    [[nodiscard]] std::uint32_t BindlessIndex(BufferHandle buffer) const override;
    [[nodiscard]] std::uint32_t BindlessIndex(TextureHandle texture) const override;

    [[nodiscard]] Result<PipelineHandle> CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle pipeline) override;

    [[nodiscard]] Result<CommandPoolHandle> CreateCommandPool() override;
    void DestroyCommandPool(CommandPoolHandle pool) override;
    void ResetCommandPool(CommandPoolHandle pool) override;
    [[nodiscard]] ICommandList& BeginCommandList(CommandPoolHandle pool) override;

    std::uint64_t Submit(std::span<ICommandList* const> commandLists) override;
    std::uint64_t Submit(std::span<ICommandList* const> commandLists, ISwapchain& swapchain) override;
    [[nodiscard]] std::uint64_t LastSubmittedValue() const override;
    [[nodiscard]] std::uint64_t CompletedValue() const override;
    void WaitForValue(std::uint64_t value) override;

    /// The VkInstance, for creating window surfaces (e.g. SdlPlatform::CreateVulkanSurface).
    [[nodiscard]] void* NativeInstance() const noexcept;
    /// A surface with no window (VK_EXT_headless_surface, which must be in instanceExtensions together with
    /// VK_KHR_surface): swapchains for tests and offscreen runs. Returns the VkSurfaceKHR handle value.
    [[nodiscard]] Result<std::uint64_t> CreateHeadlessSurface();
    /// A swapchain presenting to `surface` (a VkSurfaceKHR handle value, whose ownership passes to the swapchain).
    /// Needs VulkanDeviceDesc::swapchain. Destroy it before the device.
    [[nodiscard]] Result<std::unique_ptr<ISwapchain>> CreateSwapchain(std::uint64_t surface, const SwapchainDesc& desc);

    /// Validation-layer errors reported since creation (always 0 without validation).
    [[nodiscard]] std::uint32_t ValidationErrorCount() const noexcept;
    [[nodiscard]] std::string_view DeviceName() const noexcept;

    /// Backend state, defined in the module's private headers.
    struct Impl;
    [[nodiscard]] Impl& GetImpl() noexcept { return *m_impl; }

private:
    explicit VulkanDevice(std::unique_ptr<Impl> impl);
    std::uint64_t SubmitImpl(std::span<ICommandList* const> commandLists, ISwapchain* swapchain);

    std::unique_ptr<Impl> m_impl;
};

} // namespace interi::rhi

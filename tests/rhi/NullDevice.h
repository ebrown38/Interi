// SPDX-License-Identifier: MIT
#pragma once

#include <interi/rhi/Device.h>

#include <cstdint>

namespace interi::testing
{

/// Records nothing. For measuring CPU-side rendering code without a GPU or mock overhead.
class NullCommandList final : public rhi::ICommandList
{
public:
    void CopyBuffer(rhi::BufferHandle, std::uint64_t, rhi::BufferHandle, std::uint64_t, std::uint64_t) override {}
    void FillBuffer(rhi::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
    void Barriers(std::span<const rhi::BufferBarrier>, std::span<const rhi::TextureBarrier>) override {}
    void AliasingBarrier() override {}
    void BindPipeline(rhi::PipelineHandle) override {}
    void PushConstants(std::span<const std::byte>) override {}
    using rhi::ICommandList::PushConstants;
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void BeginRendering(const rhi::RenderingDesc&) override {}
    void EndRendering() override {}
    void SetViewport(float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
    void BindIndexBuffer(rhi::BufferHandle, std::uint64_t, rhi::IndexFormat) override {}
    void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
    void DrawIndexedIndirectCount(rhi::BufferHandle, std::uint64_t, rhi::BufferHandle, std::uint64_t, std::uint32_t,
                                  std::uint32_t) override
    {
    }
    void CopyTextureToBuffer(rhi::TextureHandle, rhi::BufferHandle, std::uint64_t, std::uint32_t) override {}
};

/// A device that hands out increasing handles, allocates no memory and completes all work instantly.
class NullDevice final : public rhi::IDevice
{
public:
    Result<rhi::BufferHandle> CreateBuffer(const rhi::BufferDesc&) override { return Next<rhi::BufferTag>(); }
    void DestroyBuffer(rhi::BufferHandle) override {}
    Result<rhi::TextureHandle> CreateTexture(const rhi::TextureDesc&) override { return Next<rhi::TextureTag>(); }
    void DestroyTexture(rhi::TextureHandle) override {}
    std::span<std::byte> MappedMemory(rhi::BufferHandle) override { return {}; }
    std::uint32_t BindlessIndex(rhi::BufferHandle buffer) const override { return buffer.Index(); }
    std::uint32_t BindlessIndex(rhi::TextureHandle texture) const override { return texture.Index(); }
    std::uint32_t BindlessStorageIndex(rhi::TextureHandle texture, std::uint32_t mip) const override
    {
        return texture.Index() + mip;
    }
    std::uint32_t BindlessIndex(rhi::SamplerHandle sampler) const override { return sampler.Index(); }
    Result<rhi::SamplerHandle> CreateSampler(const rhi::SamplerDesc&) override { return Next<rhi::SamplerTag>(); }
    void DestroySampler(rhi::SamplerHandle) override {}
    rhi::MemoryRequirements GetTextureMemoryRequirements(const rhi::TextureDesc& desc) const override
    {
        return {.size = std::uint64_t{desc.width} * desc.height * 4 * desc.arrayLayers,
                .alignment = 256,
                .memoryTypeBits = 1};
    }
    rhi::MemoryRequirements GetBufferMemoryRequirements(const rhi::BufferDesc& desc) const override
    {
        return {.size = desc.size, .alignment = 256, .memoryTypeBits = 1};
    }
    Result<rhi::HeapHandle> CreateHeap(const rhi::HeapDesc&) override { return Next<rhi::HeapTag>(); }
    void DestroyHeap(rhi::HeapHandle) override {}
    Result<rhi::TextureHandle> CreatePlacedTexture(const rhi::TextureDesc&, rhi::HeapHandle, std::uint64_t) override
    {
        return Next<rhi::TextureTag>();
    }
    Result<rhi::BufferHandle> CreatePlacedBuffer(const rhi::BufferDesc&, rhi::HeapHandle, std::uint64_t) override
    {
        return Next<rhi::BufferTag>();
    }
    Result<rhi::PipelineHandle> CreateComputePipeline(const rhi::ComputePipelineDesc&) override
    {
        return Next<rhi::PipelineTag>();
    }
    Result<rhi::PipelineHandle> CreateGraphicsPipeline(const rhi::GraphicsPipelineDesc&) override
    {
        return Next<rhi::PipelineTag>();
    }
    void DestroyPipeline(rhi::PipelineHandle) override {}
    Result<rhi::CommandPoolHandle> CreateCommandPool() override { return Next<rhi::CommandPoolTag>(); }
    void DestroyCommandPool(rhi::CommandPoolHandle) override {}
    void ResetCommandPool(rhi::CommandPoolHandle) override {}
    rhi::ICommandList& BeginCommandList(rhi::CommandPoolHandle) override { return m_commands; }
    std::uint64_t Submit(std::span<rhi::ICommandList* const>) override { return ++m_submitted; }
    std::uint64_t Submit(std::span<rhi::ICommandList* const>, rhi::ISwapchain&) override { return ++m_submitted; }
    std::uint64_t LastSubmittedValue() const override { return m_submitted; }
    std::uint64_t CompletedValue() const override { return m_submitted; }
    void WaitForValue(std::uint64_t) override {}

private:
    template <typename Tag>
    Handle<Tag> Next()
    {
        return Handle<Tag>::FromParts(++m_nextHandle, 1);
    }

    NullCommandList m_commands;
    std::uint32_t m_nextHandle = 0;
    std::uint64_t m_submitted = 0;
};

} // namespace interi::testing

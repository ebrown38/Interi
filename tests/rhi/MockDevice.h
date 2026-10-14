// SPDX-License-Identifier: MIT
#pragma once

#include <interi/rhi/Device.h>

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace interi::testing
{

class MockCommandList : public rhi::ICommandList
{
public:
    MOCK_METHOD(void, CopyBuffer,
                (rhi::BufferHandle src, std::uint64_t srcOffset, rhi::BufferHandle dst, std::uint64_t dstOffset,
                 std::uint64_t size),
                (override));
    MOCK_METHOD(void, FillBuffer,
                (rhi::BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value), (override));
    MOCK_METHOD(void, Barriers,
                (std::span<const rhi::BufferBarrier> buffers, std::span<const rhi::TextureBarrier> textures),
                (override));
    MOCK_METHOD(void, AliasingBarrier, (), (override));
    MOCK_METHOD(void, BindPipeline, (rhi::PipelineHandle pipeline), (override));
    MOCK_METHOD(void, PushConstants, (std::span<const std::byte> data), (override));
    MOCK_METHOD(void, Dispatch, (std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ), (override));
    using rhi::ICommandList::PushConstants;
    MOCK_METHOD(void, BeginRendering, (const rhi::RenderingDesc& desc), (override));
    MOCK_METHOD(void, EndRendering, (), (override));
    MOCK_METHOD(void, SetViewport, (float x, float y, float width, float height), (override));
    MOCK_METHOD(void, SetScissor, (std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height),
                (override));
    MOCK_METHOD(void, BindIndexBuffer, (rhi::BufferHandle buffer, std::uint64_t offset, rhi::IndexFormat format),
                (override));
    MOCK_METHOD(void, Draw,
                (std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                 std::uint32_t firstInstance),
                (override));
    MOCK_METHOD(void, DrawIndexed,
                (std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                 std::int32_t vertexOffset, std::uint32_t firstInstance),
                (override));
    MOCK_METHOD(void, DrawIndexedIndirectCount,
                (rhi::BufferHandle arguments, std::uint64_t argumentOffset, rhi::BufferHandle count,
                 std::uint64_t countOffset, std::uint32_t maxDraws, std::uint32_t stride),
                (override));
    MOCK_METHOD(void, CopyTextureToBuffer,
                (rhi::TextureHandle texture, rhi::BufferHandle buffer, std::uint64_t bufferOffset,
                 std::uint32_t rowPitch),
                (override));
};

class MockDevice : public rhi::IDevice
{
public:
    MOCK_METHOD(Result<rhi::BufferHandle>, CreateBuffer, (const rhi::BufferDesc& desc), (override));
    MOCK_METHOD(void, DestroyBuffer, (rhi::BufferHandle buffer), (override));
    MOCK_METHOD(Result<rhi::TextureHandle>, CreateTexture, (const rhi::TextureDesc& desc), (override));
    MOCK_METHOD(void, DestroyTexture, (rhi::TextureHandle texture), (override));
    MOCK_METHOD(std::span<std::byte>, MappedMemory, (rhi::BufferHandle buffer), (override));
    MOCK_METHOD(std::uint32_t, BindlessIndex, (rhi::BufferHandle buffer), (const, override));
    MOCK_METHOD(std::uint32_t, BindlessIndex, (rhi::TextureHandle texture), (const, override));
    MOCK_METHOD(std::uint32_t, BindlessIndex, (rhi::SamplerHandle sampler), (const, override));
    MOCK_METHOD(std::uint32_t, BindlessStorageIndex, (rhi::TextureHandle texture, std::uint32_t mip),
                (const, override));
    MOCK_METHOD(Result<rhi::SamplerHandle>, CreateSampler, (const rhi::SamplerDesc& desc), (override));
    MOCK_METHOD(void, DestroySampler, (rhi::SamplerHandle sampler), (override));
    MOCK_METHOD(rhi::MemoryRequirements, GetTextureMemoryRequirements, (const rhi::TextureDesc& desc),
                (const, override));
    MOCK_METHOD(rhi::MemoryRequirements, GetBufferMemoryRequirements, (const rhi::BufferDesc& desc), (const, override));
    MOCK_METHOD(Result<rhi::HeapHandle>, CreateHeap, (const rhi::HeapDesc& desc), (override));
    MOCK_METHOD(void, DestroyHeap, (rhi::HeapHandle heap), (override));
    MOCK_METHOD(Result<rhi::TextureHandle>, CreatePlacedTexture,
                (const rhi::TextureDesc& desc, rhi::HeapHandle heap, std::uint64_t offset), (override));
    MOCK_METHOD(Result<rhi::BufferHandle>, CreatePlacedBuffer,
                (const rhi::BufferDesc& desc, rhi::HeapHandle heap, std::uint64_t offset), (override));
    MOCK_METHOD(Result<rhi::PipelineHandle>, CreateComputePipeline, (const rhi::ComputePipelineDesc& desc), (override));
    MOCK_METHOD(Result<rhi::PipelineHandle>, CreateGraphicsPipeline, (const rhi::GraphicsPipelineDesc& desc),
                (override));
    MOCK_METHOD(void, DestroyPipeline, (rhi::PipelineHandle pipeline), (override));
    MOCK_METHOD(Result<rhi::CommandPoolHandle>, CreateCommandPool, (), (override));
    MOCK_METHOD(void, DestroyCommandPool, (rhi::CommandPoolHandle pool), (override));
    MOCK_METHOD(void, ResetCommandPool, (rhi::CommandPoolHandle pool), (override));
    MOCK_METHOD(rhi::ICommandList&, BeginCommandList, (rhi::CommandPoolHandle pool), (override));
    MOCK_METHOD(std::uint64_t, Submit, (std::span<rhi::ICommandList* const> commandLists), (override));
    MOCK_METHOD(std::uint64_t, Submit, (std::span<rhi::ICommandList* const> commandLists, rhi::ISwapchain& swapchain),
                (override));
    MOCK_METHOD(std::uint64_t, LastSubmittedValue, (), (const, override));
    MOCK_METHOD(std::uint64_t, CompletedValue, (), (const, override));
    MOCK_METHOD(void, WaitForValue, (std::uint64_t value), (override));
};

} // namespace interi::testing

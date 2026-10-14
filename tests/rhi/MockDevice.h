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
    MOCK_METHOD(void, Barriers,
                (std::span<const rhi::BufferBarrier> buffers, std::span<const rhi::TextureBarrier> textures),
                (override));
    MOCK_METHOD(void, AliasingBarrier, (), (override));
    MOCK_METHOD(void, BindPipeline, (rhi::PipelineHandle pipeline), (override));
    MOCK_METHOD(void, PushConstants, (std::span<const std::byte> data), (override));
    MOCK_METHOD(void, Dispatch, (std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ), (override));
    using rhi::ICommandList::PushConstants;
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

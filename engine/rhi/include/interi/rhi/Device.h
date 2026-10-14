// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace interi::rhi
{

class ISwapchain;

/// Records GPU commands. Obtained from IDevice::BeginCommandList and owned by its command pool: it stays valid
/// until that pool is reset. A command list is recorded by one thread at a time and submitted once.
class ICommandList
{
public:
    ICommandList() = default;
    ICommandList(const ICommandList&) = delete;
    ICommandList& operator=(const ICommandList&) = delete;
    ICommandList(ICommandList&&) = delete;
    ICommandList& operator=(ICommandList&&) = delete;
    virtual ~ICommandList() = default;

    virtual void CopyBuffer(BufferHandle src, std::uint64_t srcOffset, BufferHandle dst, std::uint64_t dstOffset,
                            std::uint64_t size) = 0;
    /// Writes the 32-bit `value` repeatedly over `size` bytes (a multiple of 4) at `offset`. Buffer in CopyDst.
    virtual void FillBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size, std::uint32_t value) = 0;
    /// Transitions resources between states. Accesses after the barrier wait for the accesses before it.
    virtual void Barriers(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures) = 0;
    /// Orders every earlier memory access before every later one. Needed when heap memory passes from one placed
    /// resource to another (aliasing); the new resource still starts from ResourceState::Undefined.
    virtual void AliasingBarrier() = 0;

    /// Binds a pipeline along with the bindless resource tables every pipeline shares.
    virtual void BindPipeline(PipelineHandle pipeline) = 0;
    /// Sets push constants (at most kMaxPushConstantSize bytes) for the following dispatches and draws.
    virtual void PushConstants(std::span<const std::byte> data) = 0;
    virtual void Dispatch(std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) = 0;

    /// Starts rendering to the attachments. Sets the viewport and scissor to the full render area.
    virtual void BeginRendering(const RenderingDesc& desc) = 0;
    virtual void EndRendering() = 0;
    /// Viewport in pixels from the top-left corner; depth range 0..1.
    virtual void SetViewport(float x, float y, float width, float height) = 0;
    virtual void SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) = 0;
    virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
                      std::uint32_t firstInstance) = 0;
    virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                             std::int32_t vertexOffset, std::uint32_t firstInstance) = 0;
    /// Draws whose arguments (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance as 5 x 32 bits,
    /// `stride` apart) are in `arguments`, and whose count is the uint32 at `countOffset` in `count`, at most
    /// `maxDraws`.
    virtual void DrawIndexedIndirectCount(BufferHandle arguments, std::uint64_t argumentOffset, BufferHandle count,
                                          std::uint64_t countOffset, std::uint32_t maxDraws, std::uint32_t stride) = 0;
    /// Copies mip 0 / layer 0 of `texture` (CopySrc) to `buffer` (CopyDst), rows `rowPitch` bytes apart. rowPitch
    /// must be a multiple of kTextureRowPitchAlignment.
    virtual void CopyTextureToBuffer(TextureHandle texture, BufferHandle buffer, std::uint64_t bufferOffset,
                                     std::uint32_t rowPitch) = 0;

    /// PushConstants for a trivially copyable struct.
    template <typename T>
    void PushConstants(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= kMaxPushConstantSize);
        PushConstants(std::as_bytes(std::span(&value, 1)));
    }
};

/// A GPU (or a fake of one). Work is ordered on a single queue whose completion is tracked by a timeline: each
/// Submit signals the next timeline value, and CompletedValue() reports how far the GPU has got.
///
/// Thread safety: resource creation/destruction, MappedMemory and BeginCommandList may be called from any thread,
/// provided each command pool is used by one thread at a time. Submit, ResetCommandPool and the waits belong to
/// the thread that drives the frame.
class IDevice
{
public:
    IDevice() = default;
    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;
    virtual ~IDevice() = default;

    [[nodiscard]] virtual Result<BufferHandle> CreateBuffer(const BufferDesc& desc) = 0;
    /// Destroys immediately: the GPU must no longer use the buffer (see ResourceManager for deferred destroy).
    virtual void DestroyBuffer(BufferHandle buffer) = 0;
    [[nodiscard]] virtual Result<TextureHandle> CreateTexture(const TextureDesc& desc) = 0;
    virtual void DestroyTexture(TextureHandle texture) = 0;
    /// The CPU view of an Upload or Readback buffer (persistently mapped). Empty for GpuOnly buffers.
    [[nodiscard]] virtual std::span<std::byte> MappedMemory(BufferHandle buffer) = 0;
    /// Index of a Storage buffer in the bindless buffer table, for shaders (kInvalidBindlessIndex otherwise).
    [[nodiscard]] virtual std::uint32_t BindlessIndex(BufferHandle buffer) const = 0;
    /// Index of a Sampled texture in the bindless texture table (kInvalidBindlessIndex otherwise).
    [[nodiscard]] virtual std::uint32_t BindlessIndex(TextureHandle texture) const = 0;
    /// Index of a sampler in the bindless sampler table.
    [[nodiscard]] virtual std::uint32_t BindlessIndex(SamplerHandle sampler) const = 0;

    [[nodiscard]] virtual Result<SamplerHandle> CreateSampler(const SamplerDesc& desc) = 0;
    virtual void DestroySampler(SamplerHandle sampler) = 0;

    [[nodiscard]] virtual MemoryRequirements GetTextureMemoryRequirements(const TextureDesc& desc) const = 0;
    [[nodiscard]] virtual MemoryRequirements GetBufferMemoryRequirements(const BufferDesc& desc) const = 0;
    [[nodiscard]] virtual Result<HeapHandle> CreateHeap(const HeapDesc& desc) = 0;
    /// The heap's memory is freed; resources placed in it must already be destroyed.
    virtual void DestroyHeap(HeapHandle heap) = 0;
    /// Resources in heap memory at `offset` (aligned to their MemoryRequirements). Destroy them with
    /// DestroyTexture/DestroyBuffer before their heap. Placed buffers are GpuOnly.
    [[nodiscard]] virtual Result<TextureHandle> CreatePlacedTexture(const TextureDesc& desc, HeapHandle heap,
                                                                    std::uint64_t offset) = 0;
    [[nodiscard]] virtual Result<BufferHandle> CreatePlacedBuffer(const BufferDesc& desc, HeapHandle heap,
                                                                  std::uint64_t offset) = 0;

    [[nodiscard]] virtual Result<PipelineHandle> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    [[nodiscard]] virtual Result<PipelineHandle> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

    [[nodiscard]] virtual Result<CommandPoolHandle> CreateCommandPool() = 0;
    virtual void DestroyCommandPool(CommandPoolHandle pool) = 0;
    /// Recycles every command list begun from `pool`. The GPU must have finished executing them.
    virtual void ResetCommandPool(CommandPoolHandle pool) = 0;
    /// Starts recording a new command list from `pool`.
    [[nodiscard]] virtual ICommandList& BeginCommandList(CommandPoolHandle pool) = 0;

    /// Ends and submits the command lists in order, then signals and returns the next timeline value.
    virtual std::uint64_t Submit(std::span<ICommandList* const> commandLists) = 0;
    /// Submit for a frame that renders to `swapchain`'s acquired image: the work waits until the image is
    /// available, and ISwapchain::Present waits for the work.
    virtual std::uint64_t Submit(std::span<ICommandList* const> commandLists, ISwapchain& swapchain) = 0;
    /// The value returned by the most recent Submit (0 before the first).
    [[nodiscard]] virtual std::uint64_t LastSubmittedValue() const = 0;
    /// The highest timeline value whose work the GPU has finished.
    [[nodiscard]] virtual std::uint64_t CompletedValue() const = 0;
    /// Blocks until CompletedValue() >= value.
    virtual void WaitForValue(std::uint64_t value) = 0;
};

} // namespace interi::rhi

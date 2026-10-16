// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Device.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::rhi
{

/// Copies CPU data into GPU buffers through a ring of fixed-size, persistently mapped staging chunks.
///
/// Each upload is written into staging memory and a copy is recorded into the caller's command list; uploads
/// larger than a chunk are split across chunks. After submitting the command list, call Submitted() with its
/// timeline value: the chunks used since the previous call are recycled once that value completes. Not
/// thread-safe; use one queue per recording thread.
class UploadQueue
{
public:
    UploadQueue(IDevice& device, std::uint64_t chunkSize);
    /// Waits for in-flight chunks, then destroys every staging buffer.
    ~UploadQueue();

    UploadQueue(const UploadQueue&) = delete;
    UploadQueue& operator=(const UploadQueue&) = delete;
    UploadQueue(UploadQueue&&) = delete;
    UploadQueue& operator=(UploadQueue&&) = delete;

    /// Stages `data` and records copies into `dst` at `dstOffset` on `commands`.
    [[nodiscard]] Result<void> UploadBuffer(ICommandList& commands, BufferHandle dst, std::uint64_t dstOffset,
                                            std::span<const std::byte> data);
    /// Stages a `width` x `height` image of `bytesPerPixel`-byte texels (`data`: tightly packed rows) at the
    /// texture row pitch alignment and records its copy into mip `mip` of layer `layer` of `texture` (in CopyDst).
    /// The whole image goes into one staging chunk: InvalidArgument when it cannot fit in one.
    [[nodiscard]] Result<void> UploadTexture(ICommandList& commands, TextureHandle texture, std::uint32_t mip,
                                             std::uint32_t layer, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t bytesPerPixel, std::span<const std::byte> data);

    /// The command lists holding this queue's copies were submitted as `timelineValue`.
    void Submitted(std::uint64_t timelineValue);

private:
    struct Chunk
    {
        BufferHandle buffer;
        std::uint64_t used = 0;
        std::uint64_t retireValue = 0;
    };

    /// Makes m_current a chunk with free space: a completed one if possible, otherwise a new one.
    [[nodiscard]] Result<void> NextChunk();

    IDevice& m_device;
    std::uint64_t m_chunkSize;
    std::vector<Chunk> m_chunks;          ///< All chunks ever created.
    std::vector<std::size_t> m_recording; ///< Chunks written since the last Submitted().
    std::size_t m_current = kNoChunk;     ///< Chunk being filled, or kNoChunk.
    static constexpr std::size_t kNoChunk = ~std::size_t{0};
};

} // namespace interi::rhi

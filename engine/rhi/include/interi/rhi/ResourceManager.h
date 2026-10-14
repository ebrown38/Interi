// SPDX-License-Identifier: MIT
#pragma once

#include <interi/rhi/Device.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <variant>
#include <vector>

namespace interi::rhi
{

/// Defers destruction of GPU resources until the GPU can no longer be using them.
///
/// A resource released while frame N is being recorded may be referenced by that frame's submission, which will
/// signal timeline value LastSubmittedValue() + 1. It is destroyed by the first Collect() after the device reports
/// that value complete. Destroy* may be called from any thread; Collect belongs to the frame thread.
class ResourceManager
{
public:
    explicit ResourceManager(IDevice& device);
    /// Waits for the GPU to finish all submitted work, then destroys every pending resource.
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    void DestroyBuffer(BufferHandle buffer);
    void DestroyTexture(TextureHandle texture);
    void DestroyPipeline(PipelineHandle pipeline);
    void DestroySampler(SamplerHandle sampler);
    /// Resources placed in the heap must be released (to this manager or directly) first.
    void DestroyHeap(HeapHandle heap);

    /// Destroys every pending resource whose frame has completed on the GPU.
    void Collect();

    [[nodiscard]] std::size_t PendingCount() const;

private:
    struct Pending
    {
        std::variant<BufferHandle, TextureHandle, PipelineHandle, HeapHandle, SamplerHandle> resource;
        std::uint64_t retireValue;
    };

    void Defer(std::variant<BufferHandle, TextureHandle, PipelineHandle, HeapHandle, SamplerHandle> resource);
    void Destroy(const Pending& pending);

    IDevice& m_device;
    mutable std::mutex m_mutex;
    std::vector<Pending> m_pending; ///< In release order, so retireValue only grows (heaps go after their resources).
};

} // namespace interi::rhi

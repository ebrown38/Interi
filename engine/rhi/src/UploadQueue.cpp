// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/rhi/UploadQueue.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace interi::rhi
{

namespace
{

constexpr std::uint64_t kRecording = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kStagingAlignment = 16;

} // namespace

UploadQueue::UploadQueue(IDevice& device, std::uint64_t chunkSize)
    : m_device(device)
    , m_chunkSize(chunkSize)
{
    INTERI_ASSERT(chunkSize >= kStagingAlignment && chunkSize % kStagingAlignment == 0);
}

UploadQueue::~UploadQueue()
{
    std::uint64_t newest = 0;
    for (const Chunk& chunk : m_chunks)
    {
        if (chunk.retireValue != kRecording)
        {
            newest = std::max(newest, chunk.retireValue);
        }
    }
    if (newest != 0)
    {
        m_device.WaitForValue(newest);
    }
    for (const Chunk& chunk : m_chunks)
    {
        m_device.DestroyBuffer(chunk.buffer);
    }
}

Result<void> UploadQueue::UploadBuffer(ICommandList& commands, BufferHandle dst, std::uint64_t dstOffset,
                                       std::span<const std::byte> data)
{
    std::uint64_t done = 0;
    while (done < data.size())
    {
        if (m_current == kNoChunk || m_chunks[m_current].used == m_chunkSize)
        {
            INTERI_TRY(NextChunk());
        }
        Chunk& chunk = m_chunks[m_current];
        const std::uint64_t size = std::min<std::uint64_t>(data.size() - done, m_chunkSize - chunk.used);
        const std::span<std::byte> mapped = m_device.MappedMemory(chunk.buffer);
        std::memcpy(mapped.data() + chunk.used, data.data() + done, size);
        commands.CopyBuffer(chunk.buffer, chunk.used, dst, dstOffset + done, size);

        chunk.used = std::min(m_chunkSize, (chunk.used + size + kStagingAlignment - 1) & ~(kStagingAlignment - 1));
        done += size;
    }
    return {};
}

void UploadQueue::Submitted(std::uint64_t timelineValue)
{
    for (const std::size_t index : m_recording)
    {
        m_chunks[index].retireValue = timelineValue;
    }
    m_recording.clear();
    m_current = kNoChunk;
}

Result<void> UploadQueue::NextChunk()
{
    const std::uint64_t completed = m_device.CompletedValue();
    for (std::size_t i = 0; i < m_chunks.size(); ++i)
    {
        Chunk& chunk = m_chunks[i];
        if (chunk.retireValue != kRecording && chunk.retireValue <= completed)
        {
            chunk.used = 0;
            chunk.retireValue = kRecording;
            m_recording.push_back(i);
            m_current = i;
            return {};
        }
    }

    INTERI_TRY_ASSIGN(buffer, m_device.CreateBuffer(BufferDesc{.size = m_chunkSize,
                                                               .usage = BufferUsage::CopySrc,
                                                               .memory = MemoryUsage::Upload,
                                                               .debugName = "UploadQueue staging"}));
    m_chunks.push_back(Chunk{.buffer = buffer, .used = 0, .retireValue = kRecording});
    m_current = m_chunks.size() - 1;
    m_recording.push_back(m_current);
    return {};
}

} // namespace interi::rhi

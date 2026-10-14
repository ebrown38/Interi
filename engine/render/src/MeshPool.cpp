// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/render/MeshPool.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace interi::render
{

MeshRange ComputeBounds(const MeshData& mesh)
{
    INTERI_ASSERT(!mesh.vertices.empty());
    Vec3 min = mesh.vertices.front().position;
    Vec3 max = min;
    for (const Vertex& vertex : mesh.vertices)
    {
        min = {std::min(min.x, vertex.position.x), std::min(min.y, vertex.position.y),
               std::min(min.z, vertex.position.z)};
        max = {std::max(max.x, vertex.position.x), std::max(max.y, vertex.position.y),
               std::max(max.z, vertex.position.z)};
    }
    MeshRange range;
    range.boundsCenter = (min + max) * 0.5f;
    for (const Vertex& vertex : mesh.vertices)
    {
        range.boundsRadius = std::max(range.boundsRadius, Length(vertex.position - range.boundsCenter));
    }
    return range;
}

Result<std::unique_ptr<MeshPool>> MeshPool::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                   std::uint32_t maxVertices, std::uint32_t maxIndices)
{
    std::unique_ptr<MeshPool> pool(new MeshPool(resources, maxVertices, maxIndices));
    INTERI_TRY_ASSIGN(vertices, device.CreateBuffer({.size = std::uint64_t{maxVertices} * sizeof(Vertex),
                                                     .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst,
                                                     .debugName = "MeshPool vertices"}));
    pool->m_vertices = vertices;
    INTERI_TRY_ASSIGN(indices, device.CreateBuffer({.size = std::uint64_t{maxIndices} * sizeof(std::uint32_t),
                                                    .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                                                    .debugName = "MeshPool indices"}));
    pool->m_indices = indices;
    return pool;
}

MeshPool::~MeshPool()
{
    if (m_vertices.IsValid())
    {
        m_resources.DestroyBuffer(m_vertices);
    }
    if (m_indices.IsValid())
    {
        m_resources.DestroyBuffer(m_indices);
    }
}

Result<MeshRange> MeshPool::Add(rhi::UploadQueue& uploads, rhi::ICommandList& commands, const MeshData& mesh)
{
    INTERI_ASSERT(!mesh.vertices.empty() && !mesh.indices.empty());
    if (m_vertexCount + mesh.vertices.size() > m_maxVertices || m_indexCount + mesh.indices.size() > m_maxIndices)
    {
        return MakeError(ErrorCode::OutOfMemory, "MeshPool is full");
    }
    MeshRange range = ComputeBounds(mesh);
    range.firstIndex = m_indexCount;
    range.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    range.vertexOffset = static_cast<std::int32_t>(m_vertexCount);

    // Earlier frames may still be drawing from the buffers: order their reads before these writes.
    const rhi::ResourceState vertexState = m_written ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined;
    const rhi::ResourceState indexState = m_written ? rhi::ResourceState::IndexBuffer : rhi::ResourceState::Undefined;
    const std::array toCopy{rhi::BufferBarrier{m_vertices, vertexState, rhi::ResourceState::CopyDst},
                            rhi::BufferBarrier{m_indices, indexState, rhi::ResourceState::CopyDst}};
    commands.Barriers(toCopy, {});
    INTERI_TRY(uploads.UploadBuffer(commands, m_vertices, std::uint64_t{m_vertexCount} * sizeof(Vertex),
                                    std::as_bytes(std::span(mesh.vertices))));
    INTERI_TRY(uploads.UploadBuffer(commands, m_indices, std::uint64_t{m_indexCount} * sizeof(std::uint32_t),
                                    std::as_bytes(std::span(mesh.indices))));
    const std::array toDraw{
        rhi::BufferBarrier{m_vertices, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead},
        rhi::BufferBarrier{m_indices, rhi::ResourceState::CopyDst, rhi::ResourceState::IndexBuffer}};
    commands.Barriers(toDraw, {});

    m_vertexCount += static_cast<std::uint32_t>(mesh.vertices.size());
    m_indexCount += static_cast<std::uint32_t>(mesh.indices.size());
    m_written = true;
    return range;
}

} // namespace interi::render

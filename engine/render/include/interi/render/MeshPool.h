// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/render/Mesh.h>
#include <interi/rhi/Types.h>

#include <cstdint>
#include <memory>

namespace interi::rhi
{
class IDevice;
class ICommandList;
class ResourceManager;
class UploadQueue;
} // namespace interi::rhi

namespace interi::render
{

/// Where a mesh lives in a MeshPool, plus its bounding sphere (mesh space) for culling.
struct MeshRange
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    Vec3 boundsCenter;
    float boundsRadius = 0.0f;
};

/// Every mesh in one vertex buffer and one index buffer, so a single index-buffer binding serves all of a frame's
/// GPU-generated (indirect) draws. Meshes are appended and never freed.
class MeshPool
{
public:
    [[nodiscard]] static Result<std::unique_ptr<MeshPool>> Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                                  std::uint32_t maxVertices, std::uint32_t maxIndices);
    ~MeshPool();

    MeshPool(const MeshPool&) = delete;
    MeshPool& operator=(const MeshPool&) = delete;
    MeshPool(MeshPool&&) = delete;
    MeshPool& operator=(MeshPool&&) = delete;

    /// Appends `mesh` and records its upload into `commands` (ready for drawing once they execute). OutOfMemory when
    /// the pool is full.
    [[nodiscard]] Result<MeshRange> Add(rhi::UploadQueue& uploads, rhi::ICommandList& commands, const MeshData& mesh);

    [[nodiscard]] rhi::BufferHandle Vertices() const noexcept { return m_vertices; }
    [[nodiscard]] rhi::BufferHandle Indices() const noexcept { return m_indices; }

private:
    MeshPool(rhi::ResourceManager& resources, std::uint32_t maxVertices, std::uint32_t maxIndices)
        : m_resources(resources)
        , m_maxVertices(maxVertices)
        , m_maxIndices(maxIndices)
    {
    }

    rhi::ResourceManager& m_resources;
    rhi::BufferHandle m_vertices;
    rhi::BufferHandle m_indices;
    std::uint32_t m_maxVertices;
    std::uint32_t m_maxIndices;
    std::uint32_t m_vertexCount = 0;
    std::uint32_t m_indexCount = 0;
    bool m_written = false; ///< Whether the buffers hold data (so the GPU may be reading them).
};

/// Bounding sphere of the vertices: centred on their bounding box.
[[nodiscard]] MeshRange ComputeBounds(const MeshData& mesh);

} // namespace interi::render

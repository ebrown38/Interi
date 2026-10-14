// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/rhi/Types.h>

#include <cstdint>
#include <vector>

namespace interi::rhi
{
class IDevice;
class ICommandList;
class ResourceManager;
class UploadQueue;
} // namespace interi::rhi

namespace interi::render
{

/// Vertex layout shaders pull from the bindless vertex buffer (see engine/render/shaders/Forward.vs.hlsl).
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};
static_assert(sizeof(Vertex) == 24, "shaders read vertices with a 24-byte stride");

/// CPU mesh: triangles are counter-clockwise when seen from outside.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// Axis-aligned cube centred on the origin, with flat per-face normals.
[[nodiscard]] MeshData MakeCube(float halfExtent = 0.5f);
/// Square in the XZ plane facing +Y.
[[nodiscard]] MeshData MakePlane(float halfExtent);

struct GpuMesh
{
    rhi::BufferHandle vertices; ///< Storage buffer (bindless).
    rhi::BufferHandle indices;  ///< 32-bit index buffer.
    std::uint32_t indexCount = 0;
};

/// Creates the buffers and records the upload into `commands`, leaving them ready for drawing.
[[nodiscard]] Result<GpuMesh> UploadMesh(rhi::IDevice& device, rhi::UploadQueue& uploads, rhi::ICommandList& commands,
                                         const MeshData& mesh);
void DestroyMesh(rhi::ResourceManager& resources, const GpuMesh& mesh);

} // namespace interi::render

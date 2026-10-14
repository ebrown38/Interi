// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/render/Mesh.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include <array>
#include <span>

namespace interi::render
{

namespace
{

/// Appends a quad with corners a, b, c, d (counter-clockwise seen from where `normal` points).
void AddQuad(MeshData& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal)
{
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec3& corner : {a, b, c, d})
    {
        mesh.vertices.push_back({corner, normal});
    }
    for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        mesh.indices.push_back(base + index);
    }
}

} // namespace

MeshData MakeCube(float halfExtent)
{
    const float h = halfExtent;
    MeshData mesh;
    AddQuad(mesh, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});      // +X
    AddQuad(mesh, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0}); // -X
    AddQuad(mesh, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});      // +Y
    AddQuad(mesh, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0}); // -Y
    AddQuad(mesh, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});      // +Z
    AddQuad(mesh, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1}); // -Z
    return mesh;
}

MeshData MakePlane(float halfExtent)
{
    const float h = halfExtent;
    MeshData mesh;
    AddQuad(mesh, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {0, 1, 0});
    return mesh;
}

Result<GpuMesh> UploadMesh(rhi::IDevice& device, rhi::UploadQueue& uploads, rhi::ICommandList& commands,
                           const MeshData& mesh)
{
    INTERI_ASSERT(!mesh.vertices.empty() && !mesh.indices.empty());
    GpuMesh gpu;
    gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    INTERI_TRY_ASSIGN(vertices, device.CreateBuffer({.size = mesh.vertices.size() * sizeof(Vertex),
                                                     .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst,
                                                     .debugName = "mesh vertices"}));
    gpu.vertices = vertices;
    INTERI_TRY_ASSIGN(indices, device.CreateBuffer({.size = mesh.indices.size() * sizeof(std::uint32_t),
                                                    .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                                                    .debugName = "mesh indices"}));
    gpu.indices = indices;

    const std::array toCopy{
        rhi::BufferBarrier{gpu.vertices, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst},
        rhi::BufferBarrier{gpu.indices, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
    commands.Barriers(toCopy, {});
    INTERI_TRY(uploads.UploadBuffer(commands, gpu.vertices, 0, std::as_bytes(std::span(mesh.vertices))));
    INTERI_TRY(uploads.UploadBuffer(commands, gpu.indices, 0, std::as_bytes(std::span(mesh.indices))));
    const std::array toDraw{
        rhi::BufferBarrier{gpu.vertices, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead},
        rhi::BufferBarrier{gpu.indices, rhi::ResourceState::CopyDst, rhi::ResourceState::IndexBuffer}};
    commands.Barriers(toDraw, {});
    return gpu;
}

void DestroyMesh(rhi::ResourceManager& resources, const GpuMesh& mesh)
{
    resources.DestroyBuffer(mesh.vertices);
    resources.DestroyBuffer(mesh.indices);
}

} // namespace interi::render

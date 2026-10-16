// SPDX-License-Identifier: MIT
#include <interi/destruction_render/ChunkMeshCache.h>

#include <algorithm>

namespace interi::destruction
{

Result<void> ChunkMeshCache::Add(rhi::UploadQueue& uploads, rhi::ICommandList& commands,
                                 const std::shared_ptr<const DestructibleAsset>& asset)
{
    if (Find(asset.get()) != nullptr)
    {
        return {};
    }
    auto meshes = std::make_unique<AssetMeshes>(
        AssetMeshes{.asset = asset, .firstSubmesh = {}, .submeshes = {}, .renderSet = ChunkRenderSet(asset->chunks)});
    render::MeshData data;
    for (const AssetChunk& chunk : asset->chunks)
    {
        meshes->firstSubmesh.push_back(static_cast<std::uint32_t>(meshes->submeshes.size()));
        if (chunk.render.indices.empty())
        {
            continue;
        }
        data.vertices.clear();
        for (const RenderVertex& vertex : chunk.render.vertices)
        {
            data.vertices.push_back({.position = vertex.position, .normal = vertex.normal});
        }
        data.indices = chunk.render.indices;
        INTERI_TRY_ASSIGN(range, m_meshes.Add(uploads, commands, data));
        for (const Submesh& submesh : chunk.render.submeshes)
        {
            render::MeshRange part = range;
            part.firstIndex += submesh.indexOffset;
            part.indexCount = submesh.indexCount;
            meshes->submeshes.push_back(
                {.mesh = part, .surfaceMaterial = submesh.surfaceMaterial, .interior = submesh.interior});
        }
    }
    meshes->firstSubmesh.push_back(static_cast<std::uint32_t>(meshes->submeshes.size()));
    m_assets.push_back(std::move(meshes));
    return {};
}

AssetMeshes* ChunkMeshCache::Find(const DestructibleAsset* asset) noexcept
{
    const auto found =
        std::ranges::find_if(m_assets, [asset](const auto& meshes) { return meshes->asset.get() == asset; });
    return found != m_assets.end() ? found->get() : nullptr;
}

} // namespace interi::destruction

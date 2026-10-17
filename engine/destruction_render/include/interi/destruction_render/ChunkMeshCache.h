// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/ChunkRenderSet.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/render/MeshPool.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace interi::rhi
{
class ICommandList;
class UploadQueue;
} // namespace interi::rhi

namespace interi::destruction
{

/// One submesh of a chunk in the MeshPool.
struct ChunkSubmesh
{
    render::MeshRange mesh{}; // NOLINT(readability-redundant-member-init): lets designated inits omit it
    std::uint32_t surfaceMaterial = 0;
    bool interior = false; ///< Faces exposed by fracture (drawn with the interior material table).
};

/// The uploaded meshes of one asset: every chunk (inner chunks too), one MeshRange per submesh.
struct AssetMeshes
{
    std::shared_ptr<const DestructibleAsset> asset;
    std::vector<std::uint32_t> firstSubmesh; ///< Per chunk, plus one past the end: chunk c owns [c], [c + 1]).
    std::vector<ChunkSubmesh> submeshes;
    ChunkRenderSet renderSet; ///< Scratch for picking chunks: not thread-safe.

    [[nodiscard]] std::span<const ChunkSubmesh> Submeshes(std::uint32_t chunk) const
    {
        return std::span(submeshes).subspan(firstSubmesh[chunk], firstSubmesh[chunk + 1] - firstSubmesh[chunk]);
    }
};

/// The render meshes of destructible assets in a MeshPool, uploaded once per asset and kept for the cache's life
/// (the pool never frees). Holds a reference to each asset.
class ChunkMeshCache
{
public:
    explicit ChunkMeshCache(render::MeshPool& meshes)
        : m_meshes(meshes)
    {
    }

    /// Uploads every chunk of `asset` unless it is already cached; the uploads are recorded into `commands`.
    /// OutOfMemory when the MeshPool is full (the asset is then not cached).
    [[nodiscard]] Result<void> Add(rhi::UploadQueue& uploads, rhi::ICommandList& commands,
                                   const std::shared_ptr<const DestructibleAsset>& asset);

    /// The meshes of `asset`, or null when it was not added.
    [[nodiscard]] AssetMeshes* Find(const DestructibleAsset* asset) noexcept;

    [[nodiscard]] std::size_t AssetCount() const noexcept { return m_assets.size(); }

    /// Forgets every asset (for clearing the MeshPool, whose ranges it holds; re-add what is still drawn).
    void Clear() noexcept { m_assets.clear(); }

private:
    render::MeshPool& m_meshes;
    std::vector<std::unique_ptr<AssetMeshes>> m_assets;
};

} // namespace interi::destruction

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction_render/ChunkMeshCache.h>
#include <interi/ecs/Query.h>
#include <interi/render/ForwardRenderer.h>

#include <cstdint>
#include <vector>

namespace interi
{
class World;
}

namespace interi::destruction
{

class DestructionWorld;

/// Render materials of chunk submeshes.
struct ChunkMaterials
{
    std::vector<render::Material> surface;  ///< By surface (glTF) material index.
    std::vector<render::Material> interior; ///< By DestructionMaterial::interiorMaterialId (fracture faces).
    render::Material fallback;              ///< For indices outside the tables.

    [[nodiscard]] const render::Material& Get(const ChunkSubmesh& submesh) const noexcept
    {
        const std::vector<render::Material>& table = submesh.interior ? interior : surface;
        return submesh.surfaceMaterial < table.size() ? table[submesh.surfaceMaterial] : fallback;
    }
};

struct ChunkGatherStats
{
    std::uint32_t pieces = 0;   ///< Destructible pieces drawn.
    std::uint32_t debris = 0;   ///< Debris bodies and particles drawn.
    std::uint32_t rubble = 0;   ///< Rubble pile members drawn.
    std::uint32_t chunks = 0;   ///< Chunk meshes drawn (each one RenderObject per submesh).
    std::uint32_t uncached = 0; ///< Pieces, debris or rubble parts skipped: their asset is not in the cache.
    std::uint32_t dropped = 0;  ///< RenderObjects not appended because maxObjects was reached.
};

/// Turns destructibles into forward-renderer objects. Each set of leaf chunks that moves together (a piece of an
/// instance, a debris body or particle, a rubble pile member) is drawn with the fewest chunk meshes that cover it
/// (ChunkRenderSet): intact structures draw their root meshes, and only regions that broke apart draw smaller
/// chunks. Every chunk mesh becomes one RenderObject per submesh, at its body's transform.
///
/// Anchored pieces are occluders. Debris takes its DebrisComponent opacity (fading) and is skipped when not
/// visible. Not thread-safe (uses the cache's scratch).
class ChunkGatherer
{
public:
    ChunkGatherer();

    /// Appends objects to `out` (which keeps its contents) until it holds `maxObjects`.
    ChunkGatherStats Gather(World& world, const DestructionWorld& destruction, ChunkMeshCache& cache,
                            const ChunkMaterials& materials, std::vector<render::RenderObject>& out,
                            std::size_t maxObjects);

private:
    Query m_debris;
    Query m_rubble;
    std::vector<std::uint32_t> m_cover;
};

} // namespace interi::destruction

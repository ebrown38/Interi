// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/destruction/ChunkHierarchy.h>

#include "Vec3d.h"

#include <algorithm>
#include <map>

namespace interi::destruction
{
namespace
{

Chunk MakeChunk(FractureMesh mesh, std::uint32_t parent, std::uint32_t level)
{
    Chunk chunk;
    chunk.volume = ComputeVolume(mesh);
    chunk.centroid = ComputeCentroid(mesh);
    chunk.material = DominantMaterial(mesh);
    chunk.mesh = std::move(mesh);
    chunk.parent = parent;
    chunk.level = level;
    return chunk;
}

} // namespace

std::uint32_t ChunkHierarchy::Depth() const noexcept
{
    std::uint32_t depth = 0;
    for (const Chunk& chunk : chunks)
    {
        depth = std::max(depth, chunk.level);
    }
    return depth;
}

ChunkHierarchy BuildHierarchy(const FractureMesh& source, const HierarchySettings& settings, JobSystem& jobs)
{
    ChunkHierarchy hierarchy;
    if (source.triangles.empty())
    {
        return hierarchy;
    }
    hierarchy.chunks.push_back(MakeChunk(source, kNoChunk, 0));

    const auto depth = std::min<std::size_t>(settings.maxDepth, settings.levels.size());
    std::size_t levelBegin = 0;
    for (std::size_t level = 0; level < depth; ++level)
    {
        const std::size_t levelEnd = hierarchy.chunks.size();
        for (std::size_t c = levelBegin; c < levelEnd; ++c)
        {
            SeedSettings seedSettings = settings.levels[level];
            seedSettings.randomSeed = HashCombine(seedSettings.randomSeed, c);
            const FractureMesh& mesh = hierarchy.chunks[c].mesh;
            const std::vector<Vec3> seeds = GenerateSeeds(mesh, seedSettings);
            FractureResult result = VoronoiFracture(mesh, seeds, settings.voronoi, jobs);
            if (result.pieces.size() <= 1)
            {
                continue;
            }
            const auto firstChild = static_cast<std::uint32_t>(hierarchy.chunks.size());
            for (FracturePiece& piece : result.pieces)
            {
                hierarchy.chunks.push_back(MakeChunk(std::move(piece.mesh), static_cast<std::uint32_t>(c),
                                                     static_cast<std::uint32_t>(level + 1)));
            }
            hierarchy.chunks[c].firstChild = firstChild;
            hierarchy.chunks[c].childCount = static_cast<std::uint32_t>(result.pieces.size());
        }
        levelBegin = levelEnd;
    }
    return hierarchy;
}

std::uint32_t DominantMaterial(const FractureMesh& mesh) noexcept
{
    std::map<std::uint32_t, double> areas;
    for (const FractureTriangle& t : mesh.triangles)
    {
        const auto a = detail::Vec3d::From(mesh.positions[t.indices[0]]);
        const auto b = detail::Vec3d::From(mesh.positions[t.indices[1]]);
        const auto c = detail::Vec3d::From(mesh.positions[t.indices[2]]);
        areas[t.material] += detail::Length(detail::Cross(b - a, c - a));
    }
    std::uint32_t best = 0;
    double bestArea = -1.0;
    for (const auto& [material, area] : areas)
    {
        if (area > bestArea)
        {
            best = material;
            bestArea = area;
        }
    }
    return best;
}

} // namespace interi::destruction

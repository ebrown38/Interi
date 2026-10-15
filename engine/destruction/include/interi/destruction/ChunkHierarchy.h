// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/FractureMesh.h>
#include <interi/destruction/Voronoi.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

inline constexpr std::uint32_t kNoChunk = std::numeric_limits<std::uint32_t>::max();

/// One node of a chunk hierarchy. A chunk's children partition it (up to discarded fragments), so an intact
/// section can be rendered and simulated as its ancestor.
struct Chunk
{
    FractureMesh mesh;
    std::uint32_t parent = kNoChunk;
    std::uint32_t firstChild = 0; ///< Children are contiguous: [firstChild, firstChild + childCount).
    std::uint32_t childCount = 0;
    std::uint32_t level = 0;    ///< 0 = the whole source object.
    std::uint32_t material = 0; ///< DestructionMaterial with the largest surface share.
    double volume = 0.0;        ///< m^3.
    Vec3 centroid;

    [[nodiscard]] bool IsLeaf() const noexcept { return childCount == 0; }
};

struct HierarchySettings
{
    /// levels[k] splits every chunk of level k into the chunks of level k + 1. A chunk whose split yields a
    /// single piece stays a leaf. Each chunk's seeds use HashCombine(levels[k].randomSeed, chunk index).
    std::vector<SeedSettings> levels;
    std::uint32_t maxDepth = 2; ///< Deepest level created (levels beyond it are ignored).
    VoronoiSettings voronoi;
};

/// Chunks in breadth-first order: chunk 0 is the whole source, every level follows the previous one and siblings
/// are contiguous, so a parent's index is always below its children's.
struct ChunkHierarchy
{
    std::vector<Chunk> chunks;

    [[nodiscard]] std::uint32_t Depth() const noexcept;
};

/// Builds the hierarchy of one source object by repeated Voronoi fracture. Deterministic for any worker count.
[[nodiscard]] ChunkHierarchy BuildHierarchy(const FractureMesh& source, const HierarchySettings& settings,
                                            JobSystem& jobs);

/// Material with the largest total triangle area (lowest index on ties).
[[nodiscard]] std::uint32_t DominantMaterial(const FractureMesh& mesh) noexcept;

} // namespace interi::destruction

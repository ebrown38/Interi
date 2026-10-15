// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/destruction/FractureMesh.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

enum class SeedPattern : std::uint8_t
{
    Uniform,   ///< `count` points uniformly inside the mesh.
    Clustered, ///< `count` points inside the mesh within `clusterRadius` of `impactPoint`, denser near it.
    Grid,      ///< One jittered point per `gridCellSize` cell inside the mesh; long cells give planks (wood).
};

struct SeedSettings
{
    SeedPattern pattern = SeedPattern::Uniform;
    std::uint32_t count = 16;     ///< Uniform and Clustered.
    std::uint64_t randomSeed = 1; ///< Same seed, same points (on every platform).
    Vec3 impactPoint;             ///< Clustered.
    float clusterRadius = 1.0f;   ///< Clustered, metres.
    Vec3 gridCellSize{1, 1, 1};   ///< Grid, metres per axis.
    float jitter = 0.25f;         ///< Grid: random offset as a fraction of the cell size, in [0, 1].
};

/// Generates fracture seed points inside `mesh` (ContainsPoint) with integer-only randomness, so the result is
/// identical on every platform. Uniform and Clustered may return fewer than `count` points when the mesh fills
/// little of the sampled region (64 attempts per point).
[[nodiscard]] std::vector<Vec3> GenerateSeeds(const FractureMesh& mesh, const SeedSettings& settings);

struct VoronoiSettings
{
    /// Pieces smaller than this (m^3) are removed: a whole Voronoi cell that small has its seed dropped, so its
    /// neighbours absorb the volume exactly; a small disconnected fragment of a larger cell is discarded.
    double minPieceVolume = 0.0;
};

/// One connected, watertight piece of the fractured mesh.
struct FracturePiece
{
    FractureMesh mesh;
    std::uint32_t seed = 0; ///< Index into the seeds passed in.
    double volume = 0.0;
};

struct FractureResult
{
    std::vector<FracturePiece> pieces; ///< Ordered by seed, then by lowest triangle of the piece.
    std::uint32_t mergedSeeds = 0;     ///< Seeds dropped because their cell was below minPieceVolume.
    double discardedVolume = 0.0;      ///< Volume of small disconnected fragments that were discarded.
};

/// Splits a closed mesh into the Voronoi cells of `seeds` (each cell = the mesh cut by the bisector planes to its
/// neighbouring seeds). Cells that are disconnected (non-convex input) become several pieces. Cells are computed in
/// parallel on `jobs`; the result is identical for any worker count. Duplicate seeds are ignored.
[[nodiscard]] FractureResult VoronoiFracture(const FractureMesh& mesh, std::span<const Vec3> seeds,
                                             const VoronoiSettings& settings, JobSystem& jobs);

} // namespace interi::destruction

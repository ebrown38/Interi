// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction/Material.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace interi::destruction
{

struct ContactSettings
{
    double distanceTolerance = 1e-3; ///< m; faces this close count as touching.
    double normalTolerance = 0.999;  ///< Faces touch if dot(normalA, normalB) <= -normalTolerance.
    double minArea = 1e-4;           ///< m^2; smaller total contact between two meshes is ignored.
};

/// Face contact between two meshes: their overlapping, opposite-facing coplanar faces.
struct Contact
{
    std::uint32_t a = 0; ///< Lower mesh index.
    std::uint32_t b = 0;
    double area = 0.0;
    Vec3 centroid; ///< Area-weighted centre of the overlap.
    Vec3 normal;   ///< Unit, pointing from a into b (a's outward face normal).
};

/// Finds every pair of meshes with face contact (a spatial hash over triangles, then exact 2D overlap of each
/// opposite-facing coplanar triangle pair). Sorted by (a, b); deterministic.
[[nodiscard]] std::vector<Contact> FindContacts(std::span<const FractureMesh* const> meshes,
                                                const ContactSettings& settings);

/// Connection between two touching support chunks. Limits are forces (N): contact area times the weaker of the
/// two chunks' material strengths.
struct Bond
{
    std::uint32_t chunkA = 0; ///< Lower chunk index.
    std::uint32_t chunkB = 0;
    float area = 0.0f; ///< m^2.
    Vec3 centroid;
    Vec3 normal; ///< Unit, from chunkA into chunkB.
    float compressiveLimit = 0.0f;
    float tensileLimit = 0.0f;
    float shearLimit = 0.0f;

    friend bool operator==(const Bond&, const Bond&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, Bond& bond)
{
    archive(bond.chunkA, bond.chunkB, bond.area, bond.centroid.x, bond.centroid.y, bond.centroid.z, bond.normal.x,
            bond.normal.y, bond.normal.z, bond.compressiveLimit, bond.tensileLimit, bond.shearLimit);
}

struct BondSettings
{
    ContactSettings contact;
    /// Chunks with a downward face on the plane y = groundHeight are anchored to the ground.
    std::optional<float> groundHeight;
};

/// The support graph of a destructible: bonds between leaf chunks (across objects too) and the leaves anchored to
/// the world.
struct BondGraph
{
    std::vector<Bond> bonds;            ///< Sorted by (chunkA, chunkB).
    std::vector<std::uint32_t> anchors; ///< Ascending chunk indices.
};

/// Builds bonds between the leaf chunks of `chunks` that touch, and marks leaves touching the ground plane or any
/// of `anchorMeshes` (static world geometry, e.g. terrain) as anchors.
[[nodiscard]] BondGraph BuildBondGraph(std::span<const Chunk> chunks, std::span<const DestructionMaterial> materials,
                                       const BondSettings& settings, std::span<const FractureMesh> anchorMeshes = {});

/// Number of connected components among the leaf chunks when linked by `bonds`.
[[nodiscard]] std::size_t CountLeafComponents(std::span<const Chunk> chunks, std::span<const Bond> bonds);

} // namespace interi::destruction

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/CollisionHulls.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction/Material.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

/// Render vertex of a cooked chunk.
struct RenderVertex
{
    Vec3 position;
    Vec3 normal;
    Uv uv;

    friend bool operator==(const RenderVertex&, const RenderVertex&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, RenderVertex& vertex)
{
    archive(vertex.position, vertex.normal, vertex.uv.u, vertex.uv.v);
}

/// A range of a chunk's indices drawn with one render material.
struct Submesh
{
    std::uint32_t surfaceMaterial = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;

    friend bool operator==(const Submesh&, const Submesh&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, Submesh& submesh)
{
    archive(submesh.surfaceMaterial, submesh.indexOffset, submesh.indexCount);
}

/// Indexed triangle list with one submesh per render material, ordered by material.
struct RenderMesh
{
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Submesh> submeshes;

    friend bool operator==(const RenderMesh&, const RenderMesh&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, RenderMesh& mesh)
{
    archive(mesh.vertices, mesh.indices, mesh.submeshes);
}

/// Builds the render mesh of a fracture mesh: corners with identical position, normal and UV are shared, and
/// interior faces use their material's interiorMaterialId.
[[nodiscard]] RenderMesh BuildRenderMesh(const FractureMesh& mesh, std::span<const DestructionMaterial> materials);

/// A cooked chunk: hierarchy links, physical properties, collision and rendering.
struct AssetChunk
{
    std::uint32_t parent = kNoChunk;
    std::uint32_t firstChild = 0;
    std::uint32_t childCount = 0;
    std::uint32_t level = 0;
    std::uint32_t material = 0;
    float volume = 0.0f; ///< m^3.
    float mass = 0.0f;   ///< kg.
    Vec3 centroid;
    std::vector<CollisionHull> hulls;
    RenderMesh render;

    friend bool operator==(const AssetChunk&, const AssetChunk&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, AssetChunk& chunk)
{
    archive(chunk.parent, chunk.firstChild, chunk.childCount, chunk.level, chunk.material, chunk.volume, chunk.mass,
            chunk.centroid, chunk.hulls, chunk.render);
}

/// A cooked destructible: everything the runtime needs, with no reference to the source data.
struct DestructibleAsset
{
    std::vector<DestructionMaterial> materials;
    std::vector<AssetChunk> chunks; ///< Hierarchies of all source objects, breadth-first per object.
    std::vector<Bond> bonds;        ///< Between leaf chunks.
    std::vector<std::uint32_t> anchors;

    friend bool operator==(const DestructibleAsset&, const DestructibleAsset&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, DestructibleAsset& asset)
{
    archive(asset.materials, asset.chunks, asset.bonds, asset.anchors);
}

/// Turns a fractured hierarchy and its bond graph into an asset: render mesh, collision hulls and mass per chunk
/// (in parallel). Fails if a chunk has an unknown material or no collision hull.
[[nodiscard]] Result<DestructibleAsset> AssembleAsset(std::span<const Chunk> chunks, BondGraph bonds,
                                                      std::vector<DestructionMaterial> materials,
                                                      const HullSettings& hullSettings, JobSystem& jobs);

inline constexpr FourCC kDestructibleMagic = MakeFourCC("IDST");
inline constexpr FourCC kDestructiblePayloadMagic = MakeFourCC("IDSP");
inline constexpr std::uint32_t kDestructibleVersion = 1;

/// Serializes an asset to the `.idest` format: a header (magic, version, cook key, XXH3 of the payload) followed
/// by the payload. `cookKey` identifies the inputs it was cooked from (see the cooker).
[[nodiscard]] std::vector<std::byte> WriteDestructibleAsset(const DestructibleAsset& asset, std::uint64_t cookKey);

/// Parses an `.idest` file. Fails with Corrupt (truncated data, bad magic, payload hash mismatch) or
/// VersionMismatch (written by a newer format version).
[[nodiscard]] Result<DestructibleAsset> ReadDestructibleAsset(std::span<const std::byte> data);

/// Reads only the header's cook key (no payload validation), so a cooker can skip unchanged inputs cheaply.
[[nodiscard]] Result<std::uint64_t> ReadCookKey(std::span<const std::byte> data);

} // namespace interi::destruction

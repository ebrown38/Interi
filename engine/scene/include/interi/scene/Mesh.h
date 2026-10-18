// SPDX-License-Identifier: MIT
#pragma once

// Triangle meshes as scene content (primitives, CSG results) and the mesh asset format (.imesh): magic IMSH, version,
// the XXH3 of the payload, then the payload (its own archive, IMSP).

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/FileSystem.h>
#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace interi::scene
{

/// Welded positions, triangles counter-clockwise seen from outside, per-corner normals and a material slot per
/// triangle.
struct TriangleMesh
{
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> indices; ///< Three per triangle.
    std::vector<Vec3> normals;          ///< One per index (corner).
    std::vector<std::uint32_t> slots;   ///< One per triangle.

    [[nodiscard]] std::size_t TriangleCount() const noexcept { return indices.size() / 3; }
    [[nodiscard]] std::uint32_t SlotCount() const noexcept;

    friend bool operator==(const TriangleMesh&, const TriangleMesh&) = default;
};

/// Checks the arrays agree: indices a multiple of three and in range, one normal per index, one slot per triangle,
/// finite numbers. Corrupt with the reason.
[[nodiscard]] Result<void> ValidateMesh(const TriangleMesh& mesh);

/// Whether the mesh is a closed, consistently oriented 2-manifold: every directed edge appears exactly once and its
/// reverse exactly once, and no triangle repeats a vertex.
[[nodiscard]] bool IsClosedManifold(const TriangleMesh& mesh);

/// Signed volume (divergence theorem); positive for a closed outward-facing mesh.
[[nodiscard]] double MeshVolume(const TriangleMesh& mesh);

/// Axis-aligned bounds of the positions (min > max for an empty mesh).
struct MeshBounds
{
    Vec3 min{1.0f};
    Vec3 max{-1.0f};
};
[[nodiscard]] MeshBounds BoundsOf(const TriangleMesh& mesh);

inline constexpr FourCC kMeshMagic = MakeFourCC("IMSH");
inline constexpr std::uint32_t kMeshVersion = 1;

[[nodiscard]] std::vector<std::byte> WriteMeshAsset(const TriangleMesh& mesh);
/// Corrupt for a bad hash or an invalid mesh, VersionMismatch for a newer version.
[[nodiscard]] Result<TriangleMesh> ReadMeshAsset(std::span<const std::byte> data);
[[nodiscard]] Result<TriangleMesh> LoadMeshAsset(const IFileSystem& files, std::string_view path);

/// An object drawn with a mesh asset. Slot i is drawn with colors[min(i, size - 1)] (grey if empty).
struct MeshComponent
{
    static constexpr const char* kComponentName = "interi.Mesh";
    values::AssetRef asset;
    values::ValueList colors{.element = values::ValueType::Color, .items = {}};
};

void RegisterMeshReflection(values::ReflectionRegistry& registry);

/// The colour of `slot` from a colors list (see PrimitiveComponent and MeshComponent).
[[nodiscard]] values::Color SlotColor(const values::ValueList& colors, std::uint32_t slot);

} // namespace interi::scene

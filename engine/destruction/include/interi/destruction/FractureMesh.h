// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Geometry.h>
#include <interi/core/math/Vec.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

/// Texture coordinate.
struct Uv
{
    float u = 0.0f;
    float v = 0.0f;

    friend constexpr bool operator==(const Uv&, const Uv&) = default;
};

/// One triangle of a FractureMesh. Topology lives in `indices` (shared positions); shading attributes are per
/// corner, so hard edges need no duplicated positions.
struct FractureTriangle
{
    std::array<std::uint32_t, 3> indices{}; ///< Counter-clockwise seen from outside.
    std::array<Vec3, 3> normals{};
    std::array<Uv, 3> uvs{};
    std::uint32_t material = 0;        ///< Index of the DestructionMaterial of the solid this face bounds.
    std::uint32_t surfaceMaterial = 0; ///< Render material of an exterior face (interior faces use the material's
                                       ///< interiorMaterialId).
    bool interior = false;             ///< Created by fracture (a cut surface).

    friend bool operator==(const FractureTriangle&, const FractureTriangle&) = default;
};

/// Closed triangle mesh used for fracture: positions are shared between triangles (welded), so an edge is the same
/// index pair in both triangles that use it. Several disjoint closed shells (parts) may share one mesh as long as
/// they do not overlap.
struct FractureMesh
{
    std::vector<Vec3> positions;
    std::vector<FractureTriangle> triangles;

    friend bool operator==(const FractureMesh&, const FractureMesh&) = default;
};

/// Double-precision plane n·p + d = 0 used for cutting. The normal need not be unit length; only signs and ratios
/// of distances are used.
struct CutPlane
{
    std::array<double, 3> normal{0.0, 1.0, 0.0};
    double d = 0.0;

    /// Plane through `point` with `normal`.
    [[nodiscard]] static CutPlane FromPointNormal(const std::array<double, 3>& point,
                                                  const std::array<double, 3>& normal) noexcept
    {
        return {.normal = normal, .d = -(normal[0] * point[0] + normal[1] * point[1] + normal[2] * point[2])};
    }

    /// Signed distance scaled by |normal|.
    [[nodiscard]] double Evaluate(const Vec3& p) const noexcept
    {
        return normal[0] * static_cast<double>(p.x) + normal[1] * static_cast<double>(p.y) +
               normal[2] * static_cast<double>(p.z) + d;
    }
};

/// Volume enclosed by a closed, outward-oriented mesh (divergence theorem, in double precision).
[[nodiscard]] double ComputeVolume(const FractureMesh& mesh) noexcept;
/// Centroid of the enclosed solid; the bounding-box centre for a mesh with no volume.
[[nodiscard]] Vec3 ComputeCentroid(const FractureMesh& mesh) noexcept;
[[nodiscard]] AABB ComputeBounds(const FractureMesh& mesh) noexcept;
/// Total area of the faces flagged `interior`.
[[nodiscard]] double ComputeInteriorArea(const FractureMesh& mesh) noexcept;

/// True if every edge is used by exactly two triangles, once in each direction (a closed, consistently oriented
/// 2-manifold surface), and no triangle repeats a vertex.
[[nodiscard]] bool IsWatertight(const FractureMesh& mesh);

/// Ray-parity point-in-solid test.
[[nodiscard]] bool ContainsPoint(const FractureMesh& mesh, const Vec3& point) noexcept;

/// Splits a mesh into its edge-connected pieces, ordered by their lowest triangle index. Unused positions are
/// dropped and the rest keep their relative order.
[[nodiscard]] std::vector<FractureMesh> SplitConnectedComponents(const FractureMesh& mesh);

/// Copies `mesh` dropping positions no triangle uses (relative order kept).
[[nodiscard]] FractureMesh CompactPositions(const FractureMesh& mesh);

/// Merges positions that are bit-identical, so separately built shells that share corners become one surface.
[[nodiscard]] FractureMesh WeldIdenticalPositions(const FractureMesh& mesh);

/// Distance tolerance for cutting a mesh: proportional to its largest coordinate magnitude, i.e. to float
/// rounding of its positions.
[[nodiscard]] double CutTolerance(const FractureMesh& mesh) noexcept;

/// Keeps the part of a closed mesh behind the plane (Evaluate(p) <= 0) and closes the opening with cap faces
/// lying on the plane, facing along the plane normal.
///
/// - Every edge crossing the plane gets exactly one new position, interpolated in a fixed order, so the result is
///   watertight by construction (no welding tolerance).
/// - Positions within `tolerance` of the plane count as on it. Faces lying on the plane are kept only if they face
///   along the normal.
/// - Caps may be non-convex and have holes (ear clipping with hole bridging). They are flagged `interior`, inherit
///   the material of an adjacent face, get the plane normal and planar UVs in metres.
///
/// The result may be empty (everything in front) or have several connected pieces.
[[nodiscard]] FractureMesh CutMesh(const FractureMesh& mesh, const CutPlane& plane, double tolerance);

/// Axis-aligned box mesh with flat normals and per-face UVs, for tests and tools.
[[nodiscard]] FractureMesh MakeBoxMesh(const Vec3& min, const Vec3& max, std::uint32_t material = 0);

/// Prism: the polygon `outline` (x, y; counter-clockwise, simple, may be non-convex) extruded from zMin to zMax.
[[nodiscard]] FractureMesh MakePrismMesh(std::span<const std::array<float, 2>> outline, float zMin, float zMax,
                                         std::uint32_t material = 0);

/// Appends `other` to `mesh` as additional shells (indices offset).
void AppendMesh(FractureMesh& mesh, const FractureMesh& other);

} // namespace interi::destruction

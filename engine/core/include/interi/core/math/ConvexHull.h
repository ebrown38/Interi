// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Geometry.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace interi
{

/// A planar face of a hull: one or more coplanar triangles merged together.
struct HullFace
{
    Plane plane;                          ///< Outward-facing.
    std::vector<std::uint32_t> triangles; ///< Indices into ConvexHull::triangles.
};

/// Closed convex polyhedron.
struct ConvexHull
{
    std::vector<Vec3> vertices;                          ///< Extreme points only (no points on faces/edges).
    std::vector<std::array<std::uint32_t, 3>> triangles; ///< Counter-clockwise seen from outside.
    std::vector<HullFace> faces;                         ///< Coplanar triangles merged.
    float epsilon = 0.0f;                                ///< Distance tolerance used, scaled to the input.
};

/// Builds the convex hull of a point cloud with Quickhull (Barber, Dobkin, Huhdanpaa 1996). Points within the
/// tolerance of a face (interior, on faces or edges, duplicates) are not hull vertices.
///
/// Fails with ErrorCode::Degenerate when the points do not clearly span 3D: fewer than 4 points, coincident,
/// collinear, or thinner than ~1000 * epsilon (about 0.04% of the cloud's half-extent sum). Below that the
/// tolerance-based construction cannot keep every input point within a few epsilon of the hull.
///
/// epsilon = 3 * FLT_EPSILON * (sum of the cloud's half extents); planes are evaluated in double precision.
[[nodiscard]] Result<ConvexHull> BuildConvexHull(std::span<const Vec3> points);

} // namespace interi

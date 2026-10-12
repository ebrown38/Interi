// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Geometry.h>

#include <span>
#include <vector>

namespace interi
{

/// Splits a convex polygon (vertices in order) by a plane into the part in front of it (SignedDistance >= 0) and
/// the part behind it. Vertices within `epsilon` of the plane count as on it and go to both sides' boundaries
/// without creating slivers. A side that receives fewer than 3 vertices is left empty. Outputs are cleared first.
void SplitPolygon(std::span<const Vec3> polygon, const Plane& plane, std::vector<Vec3>& front, std::vector<Vec3>& back,
                  float epsilon = 1e-6f);

/// Keeps the part of a convex polygon in front of the plane (Sutherland-Hodgman, one plane). `out` is cleared.
void ClipPolygon(std::span<const Vec3> polygon, const Plane& plane, std::vector<Vec3>& out, float epsilon = 1e-6f);

} // namespace interi

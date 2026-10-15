// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction::detail
{

struct Point2
{
    double x = 0.0;
    double y = 0.0;
};

/// Triangulates a planar region bounded by closed loops of indices into `points`. Counter-clockwise loops are
/// outer boundaries and clockwise loops are holes; each hole is attached to the smallest outer loop containing
/// it. Holes are bridged into their outer loop (Eberly), then the polygon is ear clipped. Every loop vertex is
/// used, so the triangles share the loops' edges exactly. Output triangles are counter-clockwise; loops with no
/// area are skipped.
[[nodiscard]] std::vector<std::array<std::uint32_t, 3>>
TriangulateLoops(std::span<const Point2> points, const std::vector<std::vector<std::uint32_t>>& loops);

} // namespace interi::destruction::detail

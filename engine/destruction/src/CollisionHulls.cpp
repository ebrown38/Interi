// SPDX-License-Identifier: MIT
#include <interi/core/math/ConvexHull.h>
#include <interi/destruction/CollisionHulls.h>

#include "Vec3d.h"

#include <array>
#include <limits>

namespace interi::destruction
{
namespace
{

using detail::Vec3d;

struct HullCandidate
{
    std::vector<Vec3> points;
    double volume = 0.0;
};

/// Hull of a mesh's positions; empty points when degenerate.
HullCandidate MakeHull(const FractureMesh& mesh)
{
    const Result<ConvexHull> hull = BuildConvexHull(mesh.positions);
    if (!hull)
    {
        return {};
    }
    HullCandidate candidate;
    candidate.points = hull->vertices;
    const Vec3d r = Vec3d::From(hull->vertices[0]);
    for (const auto& [a, b, c] : hull->triangles)
    {
        const Vec3d pa = Vec3d::From(hull->vertices[a]) - r;
        const Vec3d pb = Vec3d::From(hull->vertices[b]) - r;
        const Vec3d pc = Vec3d::From(hull->vertices[c]) - r;
        candidate.volume += detail::Dot(pa, detail::Cross(pb, pc)) / 6.0;
    }
    return candidate;
}

void Decompose(const FractureMesh& mesh, const HullSettings& settings, std::uint32_t depth,
               std::vector<CollisionHull>& out)
{
    HullCandidate hull = MakeHull(mesh);
    if (hull.points.empty())
    {
        return;
    }
    const double volume = ComputeVolume(mesh);
    if (depth >= settings.maxDepth || hull.volume <= volume * (1.0 + settings.maxConcavity))
    {
        out.push_back({std::move(hull.points)});
        return;
    }

    // Split through the centroid along the axis that leaves the least empty hull space.
    const Vec3 centroid = ComputeCentroid(mesh);
    const double tolerance = CutTolerance(mesh);
    std::array<FractureMesh, 2> bestSides;
    double bestVolume = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        std::array<double, 3> normal{};
        normal[static_cast<std::size_t>(axis)] = 1.0;
        const CutPlane plane = CutPlane::FromPointNormal(
            {static_cast<double>(centroid.x), static_cast<double>(centroid.y), static_cast<double>(centroid.z)},
            normal);
        const CutPlane flipped{.normal = {-normal[0], -normal[1], -normal[2]}, .d = -plane.d};
        std::array<FractureMesh, 2> sides{CutMesh(mesh, plane, tolerance), CutMesh(mesh, flipped, tolerance)};
        if (sides[0].triangles.empty() || sides[1].triangles.empty())
        {
            continue;
        }
        const double total = MakeHull(sides[0]).volume + MakeHull(sides[1]).volume;
        if (total < bestVolume)
        {
            bestVolume = total;
            bestSides = std::move(sides);
        }
    }
    if (bestSides[0].triangles.empty())
    {
        out.push_back({std::move(hull.points)}); // no axis splits it: keep the coarse hull
        return;
    }
    for (const FractureMesh& side : bestSides)
    {
        for (const FractureMesh& part : SplitConnectedComponents(side))
        {
            Decompose(part, settings, depth + 1, out);
        }
    }
}

} // namespace

Result<std::vector<CollisionHull>> BuildCollisionHulls(const FractureMesh& mesh, const HullSettings& settings)
{
    std::vector<CollisionHull> hulls;
    for (const FractureMesh& part : SplitConnectedComponents(mesh))
    {
        Decompose(part, settings, 0, hulls);
    }
    if (hulls.empty())
    {
        return MakeError(ErrorCode::Degenerate, "mesh is too thin for a collision hull");
    }
    return hulls;
}

double ConvexHullVolume(const std::vector<Vec3>& points)
{
    FractureMesh mesh;
    mesh.positions = points;
    return MakeHull(mesh).volume;
}

} // namespace interi::destruction

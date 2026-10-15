// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/FractureMesh.h>

#include <cstdint>
#include <vector>

namespace interi::destruction
{

/// One convex collision piece: the vertices of its convex hull (local to the asset, like the chunk mesh).
struct CollisionHull
{
    std::vector<Vec3> points;
    friend bool operator==(const CollisionHull&, const CollisionHull&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, CollisionHull& hull)
{
    archive(hull.points);
}

struct HullSettings
{
    /// A piece whose convex hull exceeds its own volume by more than this fraction is split further.
    double maxConcavity = 0.15;
    std::uint32_t maxDepth = 4; ///< Split levels; at most 2^maxDepth hulls per chunk.
};

/// Approximate convex decomposition of a closed mesh: if its convex hull is much larger than the mesh, the mesh is
/// cut through its centroid (by whichever axis-aligned plane gives the smallest total hull volume) and each
/// connected part is processed again. Parts too thin for a hull are skipped. Fails with Degenerate if no part
/// yields a hull.
[[nodiscard]] Result<std::vector<CollisionHull>> BuildCollisionHulls(const FractureMesh& mesh,
                                                                     const HullSettings& settings = {});

/// Volume enclosed by the convex hull of `points` (0 when the points are degenerate).
[[nodiscard]] double ConvexHullVolume(const std::vector<Vec3>& points);

} // namespace interi::destruction

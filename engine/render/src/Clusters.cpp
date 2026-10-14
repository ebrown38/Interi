// SPDX-License-Identifier: MIT
#include <interi/render/Clusters.h>

#include <algorithm>
#include <cmath>

namespace interi::render
{

float ClusterGrid::SliceStart(std::uint32_t z) const noexcept
{
    return nearPlane * std::pow(farPlane / nearPlane, static_cast<float>(z) / static_cast<float>(kSlices));
}

std::uint32_t ClusterGrid::SliceOf(float depth) const noexcept
{
    const float slice = std::log(depth / nearPlane) / std::log(farPlane / nearPlane) * static_cast<float>(kSlices);
    return static_cast<std::uint32_t>(std::clamp(slice, 0.0f, static_cast<float>(kSlices - 1)));
}

void ClusterGrid::Bounds(std::uint32_t x, std::uint32_t y, std::uint32_t z, Vec3& min, Vec3& max) const noexcept
{
    // Tile rectangle in NDC (+Y up; tile row 0 is the top of the screen).
    const float ndcX0 = -1.0f + 2.0f * static_cast<float>(x) / static_cast<float>(kTilesX);
    const float ndcX1 = -1.0f + 2.0f * static_cast<float>(x + 1) / static_cast<float>(kTilesX);
    const float ndcY1 = 1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(kTilesY);
    const float ndcY0 = 1.0f - 2.0f * static_cast<float>(y + 1) / static_cast<float>(kTilesY);
    const float depths[2] = {SliceStart(z), SliceStart(z + 1)};
    min = {INFINITY, INFINITY, -depths[1]};
    max = {-INFINITY, -INFINITY, -depths[0]};
    for (const float d : depths)
    {
        const float sx = d * tanHalfFovY * aspect;
        const float sy = d * tanHalfFovY;
        min.x = std::min({min.x, ndcX0 * sx, ndcX1 * sx});
        max.x = std::max({max.x, ndcX0 * sx, ndcX1 * sx});
        min.y = std::min({min.y, ndcY0 * sy, ndcY1 * sy});
        max.y = std::max({max.y, ndcY0 * sy, ndcY1 * sy});
    }
}

float SphereBoxMargin(Vec3 centre, float radius, Vec3 min, Vec3 max) noexcept
{
    const float dx = centre.x - std::clamp(centre.x, min.x, max.x);
    const float dy = centre.y - std::clamp(centre.y, min.y, max.y);
    const float dz = centre.z - std::clamp(centre.z, min.z, max.z);
    return dx * dx + dy * dy + dz * dz - radius * radius;
}

std::vector<std::vector<std::uint32_t>> AssignLights(const ClusterGrid& grid, std::span<const Vec3> viewPositions,
                                                     std::span<const float> radii)
{
    std::vector<std::vector<std::uint32_t>> clusters(ClusterGrid::kClusterCount);
    for (std::uint32_t z = 0; z < ClusterGrid::kSlices; ++z)
    {
        for (std::uint32_t y = 0; y < ClusterGrid::kTilesY; ++y)
        {
            for (std::uint32_t x = 0; x < ClusterGrid::kTilesX; ++x)
            {
                Vec3 min;
                Vec3 max;
                grid.Bounds(x, y, z, min, max);
                auto& list = clusters[ClusterGrid::Index(x, y, z)];
                for (std::uint32_t i = 0; i < viewPositions.size() && list.size() < ClusterGrid::kMaxLightsPerCluster;
                     ++i)
                {
                    if (SphereBoxMargin(viewPositions[i], radii[i], min, max) <= 0.0f)
                    {
                        list.push_back(i);
                    }
                }
            }
        }
    }
    return clusters;
}

} // namespace interi::render

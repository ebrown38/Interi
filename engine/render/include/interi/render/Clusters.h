// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi::render
{

/// A point light. Layout matches the GPU light buffer (32 bytes).
struct PointLight
{
    Vec3 position;
    float radius = 5.0f; ///< Influence ends here (smoothly).
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 10.0f;
};
static_assert(sizeof(PointLight) == 32);

/// Clustered forward+ light grid: the view frustum is divided into tilesX x tilesY screen tiles and `slices`
/// exponentially spaced depth slices between nearPlane and farPlane. Each cluster lists the lights touching it.
struct ClusterGrid
{
    static constexpr std::uint32_t kTilesX = 16;
    static constexpr std::uint32_t kTilesY = 9;
    static constexpr std::uint32_t kSlices = 24;
    static constexpr std::uint32_t kClusterCount = kTilesX * kTilesY * kSlices;
    static constexpr std::uint32_t kMaxLightsPerCluster = 32;
    /// Per cluster: light count, then kMaxLightsPerCluster light indices (uint32 each).
    static constexpr std::uint32_t kClusterStride = (1 + kMaxLightsPerCluster) * 4;

    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float tanHalfFovY = 1.0f;
    float aspect = 1.0f;

    /// Cluster index of (tile x, tile y from the top, slice).
    [[nodiscard]] static constexpr std::uint32_t Index(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return x + y * kTilesX + z * kTilesX * kTilesY;
    }
    /// View distance where slice `z` starts (z = kSlices gives farPlane).
    [[nodiscard]] float SliceStart(std::uint32_t z) const noexcept;
    /// Slice containing view distance `depth` (clamped to the grid).
    [[nodiscard]] std::uint32_t SliceOf(float depth) const noexcept;
    /// View-space bounding box of a cluster (the camera looks down -Z).
    void Bounds(std::uint32_t x, std::uint32_t y, std::uint32_t z, Vec3& min, Vec3& max) const noexcept;
};

/// CPU reference of the GPU light culling: for each cluster, the indices (ascending, at most kMaxLightsPerCluster)
/// of the lights whose sphere touches it. `viewPositions` are light positions in view space.
[[nodiscard]] std::vector<std::vector<std::uint32_t>>
AssignLights(const ClusterGrid& grid, std::span<const Vec3> viewPositions, std::span<const float> radii);

/// Signed distance-squared margin of the sphere-box test (dist^2 - r^2): near zero, CPU and GPU float rounding may
/// disagree.
[[nodiscard]] float SphereBoxMargin(Vec3 centre, float radius, Vec3 min, Vec3 max) noexcept;

} // namespace interi::render

// SPDX-License-Identifier: MIT
#include <interi/render/Clusters.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace interi;
using namespace interi::render;

namespace
{

ClusterGrid MakeGrid()
{
    return {.nearPlane = 0.1f, .farPlane = 100.0f, .tanHalfFovY = std::tan(0.5f), .aspect = 16.0f / 9.0f};
}

} // namespace

TEST(Clusters, Slices_AreExponentialAndCoverNearToFar)
{
    const ClusterGrid grid = MakeGrid();
    EXPECT_FLOAT_EQ(grid.SliceStart(0), 0.1f);
    EXPECT_NEAR(grid.SliceStart(ClusterGrid::kSlices), 100.0f, 1e-3f);
    const float ratio = grid.SliceStart(2) / grid.SliceStart(1);
    EXPECT_NEAR(grid.SliceStart(11) / grid.SliceStart(10), ratio, 1e-4f);
}

TEST(Clusters, SliceOf_InvertsSliceStart)
{
    const ClusterGrid grid = MakeGrid();
    for (std::uint32_t z = 0; z < ClusterGrid::kSlices; ++z)
    {
        const float middle = std::sqrt(grid.SliceStart(z) * grid.SliceStart(z + 1));
        EXPECT_EQ(grid.SliceOf(middle), z);
    }
    EXPECT_EQ(grid.SliceOf(0.01f), 0u);
    EXPECT_EQ(grid.SliceOf(1000.0f), ClusterGrid::kSlices - 1);
}

TEST(Clusters, Bounds_TopLeftTileIsUpAndLeft)
{
    const ClusterGrid grid = MakeGrid();
    Vec3 min;
    Vec3 max;
    grid.Bounds(0, 0, 10, min, max);
    EXPECT_LT(max.x, 0.0f); // left of centre
    EXPECT_GT(min.y, 0.0f); // above centre (tile row 0 is the top)
    EXPECT_LT(max.z, 0.0f); // in front of the camera
}

TEST(Clusters, AssignLights_LightOnlyInClustersItTouches)
{
    const ClusterGrid grid = MakeGrid();
    // A small light straight ahead at distance 10: in the centre tiles of the slice containing depth 10.
    const std::vector<Vec3> positions{{0.0f, 0.0f, -10.0f}};
    const std::vector<float> radii{0.5f};
    const auto clusters = AssignLights(grid, positions, radii);
    std::size_t touched = 0;
    for (std::uint32_t z = 0; z < ClusterGrid::kSlices; ++z)
    {
        for (std::uint32_t y = 0; y < ClusterGrid::kTilesY; ++y)
        {
            for (std::uint32_t x = 0; x < ClusterGrid::kTilesX; ++x)
            {
                if (!clusters[ClusterGrid::Index(x, y, z)].empty())
                {
                    ++touched;
                    EXPECT_GE(x, 6u);
                    EXPECT_LE(x, 9u);
                    EXPECT_GE(y, 3u);
                    EXPECT_LE(y, 5u);
                    EXPECT_NEAR(static_cast<float>(z), static_cast<float>(grid.SliceOf(10.0f)), 1.0f);
                }
            }
        }
    }
    EXPECT_GT(touched, 0u);
    EXPECT_FALSE(clusters[ClusterGrid::Index(7, 4, grid.SliceOf(10.0f))].empty());
}

TEST(Clusters, AssignLights_CapsLightsPerCluster)
{
    const ClusterGrid grid = MakeGrid();
    const std::vector<Vec3> positions(ClusterGrid::kMaxLightsPerCluster + 5, Vec3{0.0f, 0.0f, -5.0f});
    const std::vector<float> radii(positions.size(), 1.0f);
    const auto clusters = AssignLights(grid, positions, radii);
    const auto& centre = clusters[ClusterGrid::Index(7, 4, grid.SliceOf(5.0f))];
    ASSERT_EQ(centre.size(), ClusterGrid::kMaxLightsPerCluster);
    EXPECT_EQ(centre.front(), 0u); // ascending: the first lights win
}

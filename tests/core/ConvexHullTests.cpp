// SPDX-License-Identifier: MIT
#include <interi/core/math/ConvexHull.h>
#include <interi/testing/DeterministicRng.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

using interi::ConvexHull;
using interi::Vec3;

namespace
{
std::vector<Vec3> CubeCorners(float half = 1.0f)
{
    std::vector<Vec3> corners;
    for (int i = 0; i < 8; ++i)
    {
        corners.push_back({(i & 1) ? half : -half, (i & 2) ? half : -half, (i & 4) ? half : -half});
    }
    return corners;
}

/// Checks the structural invariants every hull must satisfy.
void ExpectValidHull(const ConvexHull& hull, const std::vector<Vec3>& input)
{
    ASSERT_GE(hull.vertices.size(), 4u);
    ASSERT_GE(hull.triangles.size(), 4u);

    // Every hull vertex is an input point.
    for (const Vec3& v : hull.vertices)
    {
        EXPECT_TRUE(std::ranges::any_of(input, [&](const Vec3& p) { return p == v; }));
    }

    // Closed 2-manifold: every directed edge appears once and its reverse appears once; Euler V - E + F = 2.
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (const auto& tri : hull.triangles)
    {
        for (std::size_t e = 0; e < 3; ++e)
        {
            EXPECT_TRUE(edges.emplace(tri[e], tri[(e + 1) % 3]).second) << "duplicate directed edge";
        }
    }
    for (const auto& [a, b] : edges)
    {
        EXPECT_TRUE(edges.contains({b, a})) << "open edge";
    }
    const auto vertexCount = static_cast<std::int64_t>(hull.vertices.size());
    const auto edgeCount = static_cast<std::int64_t>(edges.size() / 2);
    const auto faceCount = static_cast<std::int64_t>(hull.triangles.size());
    EXPECT_EQ(vertexCount - edgeCount + faceCount, 2);

    // Outward-facing, and every input point is on or behind every triangle's plane.
    Vec3 centroid{};
    for (const Vec3& v : hull.vertices)
    {
        centroid += v;
    }
    centroid /= static_cast<float>(hull.vertices.size());
    for (const auto& tri : hull.triangles)
    {
        const interi::Plane plane =
            interi::Plane::FromPoints(hull.vertices[tri[0]], hull.vertices[tri[1]], hull.vertices[tri[2]]);
        EXPECT_LT(plane.SignedDistance(centroid), 0.0f) << "triangle faces inward";
        for (const Vec3& p : input)
        {
            EXPECT_LE(plane.SignedDistance(p), hull.epsilon * 4) << "input point outside the hull";
        }
    }
}
} // namespace

TEST(ConvexHull, CubePoints_Yields6Faces12Tris)
{
    const auto cube = CubeCorners();
    const auto hull = interi::BuildConvexHull(cube);
    ASSERT_TRUE(hull.has_value()) << hull.error().message;
    EXPECT_EQ(hull->vertices.size(), 8u);
    EXPECT_EQ(hull->triangles.size(), 12u);
    EXPECT_EQ(hull->faces.size(), 6u);
    for (const auto& face : hull->faces)
    {
        EXPECT_EQ(face.triangles.size(), 2u);
        EXPECT_NEAR(interi::Length(face.plane.normal), 1.0f, 1e-5f);
    }
    ExpectValidHull(*hull, cube);
}

TEST(ConvexHull, InteriorFaceAndEdgePoints_AreDiscarded)
{
    auto points = CubeCorners();
    points.push_back({0, 0, 0});    // interior
    points.push_back({0.2f, 1, 0}); // on a face
    points.push_back({1, 1, 0});    // on an edge
    points.push_back({1, 1, 1});    // duplicate corner
    const auto hull = interi::BuildConvexHull(points);
    ASSERT_TRUE(hull.has_value()) << hull.error().message;
    EXPECT_EQ(hull->vertices.size(), 8u);
    EXPECT_EQ(hull->faces.size(), 6u);
    ExpectValidHull(*hull, points);
}

TEST(ConvexHull, DegenerateCoplanar_ReturnsError)
{
    const std::vector<Vec3> square{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5f, 0.5f, 0}};
    const auto hull = interi::BuildConvexHull(square);
    ASSERT_FALSE(hull.has_value());
    EXPECT_EQ(hull.error().code, interi::ErrorCode::Degenerate);
}

TEST(ConvexHull, TooFewCollinearOrDuplicatePoints_ReturnError)
{
    const std::vector<std::vector<Vec3>> cases{
        {},
        {{1, 2, 3}},
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}},
        {{5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}},
    };
    for (const auto& points : cases)
    {
        const auto hull = interi::BuildConvexHull(points);
        ASSERT_FALSE(hull.has_value()) << points.size() << " points";
        EXPECT_EQ(hull.error().code, interi::ErrorCode::Degenerate);
    }
}

TEST(ConvexHull, Tetrahedron_HasFourFaces)
{
    const std::vector<Vec3> points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const auto hull = interi::BuildConvexHull(points);
    ASSERT_TRUE(hull.has_value());
    EXPECT_EQ(hull->triangles.size(), 4u);
    EXPECT_EQ(hull->faces.size(), 4u);
    ExpectValidHull(*hull, points);
}

class ConvexHullRandom : public interi::testing::SeededTest
{
};

TEST_F(ConvexHullRandom, RandomClouds_SatisfyHullInvariants)
{
    for (int cloud = 0; cloud < 20; ++cloud)
    {
        std::vector<Vec3> points;
        const int count = static_cast<int>(Rng().NextInt(4, 300));
        const float scale = std::pow(10.0f, Rng().NextFloat(-2, 3)); // epsilon must scale with the input
        for (int i = 0; i < count; ++i)
        {
            points.push_back(Vec3{Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1)} * scale);
        }
        const auto hull = interi::BuildConvexHull(points);
        ASSERT_TRUE(hull.has_value()) << hull.error().message;
        ExpectValidHull(*hull, points);
    }
}

TEST_F(ConvexHullRandom, PointsOnSphere_AllBecomeVertices)
{
    std::vector<Vec3> points;
    for (int i = 0; i < 200; ++i)
    {
        points.push_back(
            interi::Normalize(Vec3{Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1)}) * 5.0f);
    }
    const auto hull = interi::BuildConvexHull(points);
    ASSERT_TRUE(hull.has_value());
    EXPECT_GT(hull->vertices.size(), 190u);
    ExpectValidHull(*hull, points);
}

TEST_F(ConvexHullRandom, NearlyCoplanarCluster_IsErrorOrValidHull)
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        std::vector<Vec3> points;
        for (int i = 0; i < 50; ++i)
        {
            points.push_back({Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1), Rng().NextFloat(-1e-6f, 1e-6f)});
        }
        const auto hull = interi::BuildConvexHull(points);
        if (hull.has_value())
        {
            ExpectValidHull(*hull, points);
        }
        else
        {
            EXPECT_EQ(hull.error().code, interi::ErrorCode::Degenerate);
        }
    }
}

TEST(ConvexHull, ThinPlate_StillBuilds)
{
    // Glass-pane proportions: 2 m x 2 m x 5 mm.
    std::vector<Vec3> points;
    for (const Vec3& corner : CubeCorners())
    {
        points.push_back({corner.x, corner.y, corner.z * 0.0025f});
    }
    const auto hull = interi::BuildConvexHull(points);
    ASSERT_TRUE(hull.has_value()) << hull.error().message;
    EXPECT_EQ(hull->vertices.size(), 8u);
    EXPECT_EQ(hull->faces.size(), 6u);
    ExpectValidHull(*hull, points);
}

TEST(ConvexHull, FarFromOrigin_SameTopologyAsAtOrigin)
{
    // The tolerance follows the cloud's extent, not its distance from the world origin.
    std::vector<Vec3> points;
    for (const Vec3& corner : CubeCorners(0.25f))
    {
        points.push_back(corner + Vec3{1000.0f, -2000.0f, 500.0f});
    }
    points.push_back(Vec3{1000.0f, -2000.0f, 500.0f}); // centre
    const auto hull = interi::BuildConvexHull(points);
    ASSERT_TRUE(hull.has_value()) << hull.error().message;
    EXPECT_EQ(hull->vertices.size(), 8u);
    EXPECT_EQ(hull->triangles.size(), 12u);
    EXPECT_EQ(hull->faces.size(), 6u);
}

// SPDX-License-Identifier: MIT
#include <interi/core/math/Clipping.h>
#include <interi/core/math/Geometry.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <vector>

using interi::AABB;
using interi::Plane;
using interi::Ray;
using interi::Vec3;

// --- AABB -----------------------------------------------------------------------------------------------------------

TEST(AABB, FromPoints_EnclosesAll)
{
    const std::vector<Vec3> points{{1, 2, 3}, {-1, 5, 0}, {4, -2, 1}};
    const AABB box = AABB::FromPoints(points);
    INTERI_EXPECT_VEC_NEAR(box.min, Vec3(-1, -2, 0), 0.0f);
    INTERI_EXPECT_VEC_NEAR(box.max, Vec3(4, 5, 3), 0.0f);
    for (const Vec3& p : points)
    {
        EXPECT_TRUE(box.Contains(p));
    }
}

TEST(AABB, Empty_IsInvalidUntilExpanded)
{
    AABB box = AABB::Empty();
    EXPECT_FALSE(box.IsValid());
    box.Expand(Vec3{1, 1, 1});
    EXPECT_TRUE(box.IsValid());
    INTERI_EXPECT_VEC_NEAR(box.Extents(), Vec3(0, 0, 0), 0.0f);
}

TEST(AABB, Intersects_TouchingCountsAndSeparatedDoesNot)
{
    const AABB a{.min = {0, 0, 0}, .max = {1, 1, 1}};
    EXPECT_TRUE(a.Intersects(AABB{.min = {1, 0, 0}, .max = {2, 1, 1}}));
    EXPECT_FALSE(a.Intersects(AABB{.min = {1.01f, 0, 0}, .max = {2, 1, 1}}));
}

TEST(AABB, CenterExtentsSurfaceArea)
{
    const AABB box{.min = {0, 0, 0}, .max = {2, 4, 6}};
    INTERI_EXPECT_VEC_NEAR(box.Center(), Vec3(1, 2, 3), 0.0f);
    INTERI_EXPECT_VEC_NEAR(box.Extents(), Vec3(1, 2, 3), 0.0f);
    EXPECT_EQ(box.SurfaceArea(), 2.0f * (8 + 24 + 12));
}

TEST(AABB, Transformed_EnclosesTransformedCorners)
{
    const AABB box{.min = {-1, -1, -1}, .max = {1, 2, 3}};
    const interi::Mat4 m =
        interi::Mat4::FromTRS(Vec3{5, 0, 0}, interi::Quat::FromAxisAngle(Vec3::UnitY(), 0.7f), Vec3{1, 2, 1});
    const AABB result = box.Transformed(m);
    for (int i = 0; i < 8; ++i)
    {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z};
        const Vec3 p = interi::TransformPoint(m, corner);
        EXPECT_TRUE(result.Contains(p, 1e-4f)) << i;
    }
}

// --- Plane ----------------------------------------------------------------------------------------------------------

TEST(Plane, FromPoints_CounterClockwiseFacesViewer)
{
    // Counter-clockwise seen from +Z, so the normal points to +Z.
    const Plane plane = Plane::FromPoints(Vec3{0, 0, 2}, Vec3{1, 0, 2}, Vec3{0, 1, 2});
    INTERI_EXPECT_VEC_NEAR(plane.normal, Vec3(0, 0, 1), 1e-6f);
    EXPECT_NEAR(plane.SignedDistance(Vec3{5, 5, 5}), 3.0f, 1e-6f);
    EXPECT_NEAR(plane.SignedDistance(Vec3{0, 0, 0}), -2.0f, 1e-6f);
}

TEST(Plane, FromPointNormal_ContainsPoint)
{
    const Plane plane = Plane::FromPointNormal(Vec3{1, 2, 3}, Vec3{0, 2, 0});
    INTERI_EXPECT_VEC_NEAR(plane.normal, Vec3(0, 1, 0), 0.0f) << "normal is normalized";
    EXPECT_NEAR(plane.SignedDistance(Vec3{7, 2, -4}), 0.0f, 1e-6f);
}

// --- Ray ------------------------------------------------------------------------------------------------------------

TEST(Ray, IntersectPlane_HitAndParallelMiss)
{
    const Plane ground = Plane::FromPointNormal(Vec3{0, 0, 0}, Vec3::UnitY());
    const auto hit = interi::Intersect(Ray{.origin = {0, 10, 0}, .direction = {0, -1, 0}}, ground);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 10.0f, 1e-6f);
    EXPECT_FALSE(interi::Intersect(Ray{.origin = {0, 10, 0}, .direction = {1, 0, 0}}, ground).has_value());
    EXPECT_FALSE(interi::Intersect(Ray{.origin = {0, 10, 0}, .direction = {0, 1, 0}}, ground).has_value())
        << "plane behind the ray";
}

TEST(Ray, IntersectAABB_HitMissAndInside)
{
    const AABB box{.min = {-1, -1, -1}, .max = {1, 1, 1}};
    const auto hit = interi::Intersect(Ray{.origin = {-5, 0, 0}, .direction = {1, 0, 0}}, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 4.0f, 1e-6f);
    EXPECT_FALSE(interi::Intersect(Ray{.origin = {-5, 3, 0}, .direction = {1, 0, 0}}, box).has_value());
    const auto inside = interi::Intersect(Ray{.origin = {0, 0, 0}, .direction = {0, 0, 1}}, box);
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(*inside, 0.0f) << "ray starting inside hits at t = 0";
}

TEST(Ray, IntersectTriangle_ReportsDistanceAndBarycentrics)
{
    const Vec3 a{0, 0, 0};
    const Vec3 b{2, 0, 0};
    const Vec3 c{0, 2, 0};
    const auto hit = interi::Intersect(Ray{.origin = {0.5f, 0.5f, 3}, .direction = {0, 0, -1}}, a, b, c);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 3.0f, 1e-6f);
    EXPECT_NEAR(hit->u, 0.25f, 1e-6f);
    EXPECT_NEAR(hit->v, 0.25f, 1e-6f);
    EXPECT_FALSE(interi::Intersect(Ray{.origin = {1.5f, 1.5f, 3}, .direction = {0, 0, -1}}, a, b, c).has_value());
}

// --- Clipping -------------------------------------------------------------------------------------------------------

namespace
{
float PolygonArea(const std::vector<Vec3>& polygon)
{
    Vec3 sum{};
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
    {
        sum += interi::Cross(polygon[i] - polygon[0], polygon[i + 1] - polygon[0]);
    }
    return 0.5f * interi::Length(sum);
}
} // namespace

TEST(Clipping, SplitTriangle_ConservesArea)
{
    const std::vector<Vec3> triangle{{0, 0, 0}, {4, 0, 0}, {0, 4, 0}};
    const Plane plane = Plane::FromPointNormal(Vec3{1, 0, 0}, Vec3::UnitX());
    std::vector<Vec3> front;
    std::vector<Vec3> back;
    interi::SplitPolygon(triangle, plane, front, back);

    ASSERT_GE(front.size(), 3u);
    ASSERT_GE(back.size(), 3u);
    EXPECT_NEAR(PolygonArea(front) + PolygonArea(back), PolygonArea(triangle), 1e-4f);
    for (const Vec3& p : front)
    {
        EXPECT_GE(plane.SignedDistance(p), -1e-5f);
    }
    for (const Vec3& p : back)
    {
        EXPECT_LE(plane.SignedDistance(p), 1e-5f);
    }
}

TEST(Clipping, PolygonEntirelyOnOneSide_IsUnchanged)
{
    const std::vector<Vec3> triangle{{2, 0, 0}, {4, 0, 0}, {2, 4, 0}};
    const Plane plane = Plane::FromPointNormal(Vec3{1, 0, 0}, Vec3::UnitX());
    std::vector<Vec3> front;
    std::vector<Vec3> back;
    interi::SplitPolygon(triangle, plane, front, back);
    EXPECT_EQ(front, triangle);
    EXPECT_TRUE(back.empty());
}

TEST(Clipping, ClipPolygon_KeepsFrontSide)
{
    const std::vector<Vec3> square{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    const Plane plane = Plane::FromPointNormal(Vec3{0, 0, 0}, Vec3::UnitY());
    std::vector<Vec3> clipped;
    interi::ClipPolygon(square, plane, clipped);
    EXPECT_NEAR(PolygonArea(clipped), 2.0f, 1e-5f);
    for (const Vec3& p : clipped)
    {
        EXPECT_GE(p.y, -1e-6f);
    }
}

class ClippingRandom : public interi::testing::SeededTest
{
};

TEST_F(ClippingRandom, RandomTrianglesAndPlanes_ConserveArea)
{
    for (int i = 0; i < 500; ++i)
    {
        auto r = [&] { return Rng().NextFloat(-5, 5); };
        const std::vector<Vec3> triangle{{r(), r(), r()}, {r(), r(), r()}, {r(), r(), r()}};
        const Plane plane = Plane::FromPointNormal(Vec3{r(), r(), r()}, interi::Normalize(Vec3{r(), r(), r() + 0.1f}));
        std::vector<Vec3> front;
        std::vector<Vec3> back;
        interi::SplitPolygon(triangle, plane, front, back);
        const float total = PolygonArea(triangle);
        EXPECT_NEAR(PolygonArea(front) + PolygonArea(back), total, 1e-3f * (1.0f + total)) << i;
    }
}

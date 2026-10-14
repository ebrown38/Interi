// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace interi;
using namespace interi::render;

namespace
{

/// Clip -> normalized device coordinates.
Vec3 Project(const Mat4& m, Vec3 point)
{
    const Vec4 clip = m * Vec4(point, 1.0f);
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace

TEST(Camera, PerspectiveReverseZ_NearPlaneMapsToOne)
{
    const Mat4 projection = PerspectiveReverseZ(std::numbers::pi_v<float> / 2, 1.0f, 0.1f);
    // Right-handed: the camera looks down -Z.
    EXPECT_NEAR(Project(projection, {0, 0, -0.1f}).z, 1.0f, 1e-6f);
}

TEST(Camera, PerspectiveReverseZ_DepthDecreasesWithDistanceTowardZero)
{
    const Mat4 projection = PerspectiveReverseZ(std::numbers::pi_v<float> / 2, 1.0f, 0.1f);
    const float near = Project(projection, {0, 0, -1.0f}).z;
    const float far = Project(projection, {0, 0, -1000.0f}).z;
    EXPECT_GT(near, far);
    EXPECT_GT(far, 0.0f);
    EXPECT_LT(far, 1e-3f); // infinite far plane: depth approaches 0
}

TEST(Camera, PerspectiveReverseZ_FieldOfViewEdgesMapToNdcEdges)
{
    // 90 degree vertical field of view: at distance 1, y = +-1 is the top/bottom edge. Aspect 2 doubles x.
    const Mat4 projection = PerspectiveReverseZ(std::numbers::pi_v<float> / 2, 2.0f, 0.1f);
    INTERI_EXPECT_VEC_NEAR(Project(projection, {0, 1, -1}), Vec3(0, 1, Project(projection, {0, 1, -1}).z), 1e-6f);
    EXPECT_NEAR(Project(projection, {2, 0, -1}).x, 1.0f, 1e-6f);
}

TEST(Camera, LookAt_TargetIsStraightAhead)
{
    const Mat4 view = LookAt({3, 4, 5}, {0, 1, 0}, {0, 1, 0});
    const Vec4 target = view * Vec4(0, 1, 0, 1);
    EXPECT_NEAR(target.x, 0.0f, 1e-5f);
    EXPECT_NEAR(target.y, 0.0f, 1e-5f);
    EXPECT_LT(target.z, 0.0f); // in front of a right-handed camera
    EXPECT_NEAR(-target.z, std::sqrt(9.0f + 9.0f + 25.0f), 1e-5f);
}

TEST(Camera, LookAt_UpStaysUp)
{
    const Mat4 view = LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Vec4 above = view * Vec4(0, 1, 0, 1);
    EXPECT_GT(above.y, 0.0f);
}

// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/render/Decals.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::render;

namespace
{

Vec3 Apply(const Mat4& m, Vec3 p)
{
    const Vec4 r = m * Vec4(p, 1.0f);
    return {r.x, r.y, r.z};
}

} // namespace

TEST(Decals, DecalBox_LocalZAlongNormalWithExtents)
{
    const Mat4 box = DecalBox({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 2.0f}, 4.0f, 0.5f);
    const Vec3 centre = Apply(box, {0, 0, 0});
    EXPECT_NEAR(centre.x, 1.0f, 1e-6f);
    EXPECT_NEAR(centre.y, 2.0f, 1e-6f);
    EXPECT_NEAR(centre.z, 3.0f, 1e-6f);
    const Vec3 front = Apply(box, {0, 0, 0.5f}); // the +z face is `along` / 2 out along the normal
    EXPECT_NEAR(front.z, 3.25f, 1e-6f);
    EXPECT_NEAR(Length(Apply(box, {0.5f, 0, 0}) - centre), 2.0f, 1e-5f);
    EXPECT_NEAR(Length(Apply(box, {0, 0.5f, 0}) - centre), 2.0f, 1e-5f);
}

TEST(Decals, DecalBox_VerticalNormal_Orthonormal)
{
    const Mat4 box = DecalBox({}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f);
    const Vec3 x(box.columns[0].x, box.columns[0].y, box.columns[0].z);
    const Vec3 y(box.columns[1].x, box.columns[1].y, box.columns[1].z);
    const Vec3 z(box.columns[2].x, box.columns[2].y, box.columns[2].z);
    EXPECT_NEAR(Dot(x, y), 0.0f, 1e-6f);
    EXPECT_NEAR(Dot(x, z), 0.0f, 1e-6f);
    EXPECT_NEAR(z.y, 1.0f, 1e-6f);
    EXPECT_NEAR(Dot(Cross(x, y), z), 1.0f, 1e-5f); // right-handed
}

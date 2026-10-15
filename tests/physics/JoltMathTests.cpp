// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/physics/JoltMath.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using interi::Mat4;
using interi::Quat;
using interi::Vec3;
using interi::Vec4;
using interi::physics::FromJolt;
using interi::physics::ToJolt;

namespace
{

constexpr float kEpsilon = 1e-5f;

const Quat kRotation = Quat::FromAxisAngle(Normalize(Vec3(1.0f, 2.0f, -0.5f)), 0.7f);

void ExpectMatNear(const Mat4& a, const Mat4& b)
{
    for (int column = 0; column < 4; ++column)
    {
        INTERI_EXPECT_VEC_NEAR(a.columns[column], b.columns[column], kEpsilon) << "column " << column;
    }
}

} // namespace

TEST(JoltMath, Vec3_RoundTrip_Exact)
{
    const Vec3 v(1.5f, -2.25f, 3.0e7f);
    EXPECT_EQ(FromJolt(ToJolt(v)), v);
    const JPH::Vec3 jolt = ToJolt(v);
    EXPECT_EQ(jolt.GetX(), v.x);
    EXPECT_EQ(jolt.GetY(), v.y);
    EXPECT_EQ(jolt.GetZ(), v.z);
}

TEST(JoltMath, Quat_RoundTrip_Exact)
{
    EXPECT_EQ(FromJolt(ToJolt(kRotation)), kRotation);
    EXPECT_EQ(ToJolt(kRotation).GetW(), kRotation.w); // (x, y, z, w) order on both sides
}

TEST(JoltMath, Mat4_RoundTrip_Exact)
{
    const Mat4 m = Mat4::FromTRS(Vec3(1.0f, 2.0f, 3.0f), kRotation, Vec3(2.0f));
    const Mat4 back = FromJolt(ToJolt(m));
    for (int column = 0; column < 4; ++column)
    {
        EXPECT_EQ(back.columns[column], m.columns[column]) << "column " << column;
    }
}

TEST(JoltMath, Mat44Translation_IsColumn3)
{
    const Vec3 t(4.0f, -5.0f, 6.0f);
    const Mat4 fromJolt = FromJolt(JPH::Mat44::sTranslation(ToJolt(t)));
    ExpectMatNear(fromJolt, Mat4::Translation(t));
    INTERI_EXPECT_VEC_NEAR(fromJolt.columns[3], Vec4(t.x, t.y, t.z, 1.0f), 0.0f);
}

TEST(JoltMath, RotationMatrices_Agree)
{
    ExpectMatNear(FromJolt(JPH::Mat44::sRotation(ToJolt(kRotation))), Mat4::Rotation(kRotation));
    INTERI_EXPECT_VEC_NEAR(FromJolt(ToJolt(kRotation) * ToJolt(Vec3(1.0f, 0.0f, 0.0f))),
                           Rotate(kRotation, Vec3(1.0f, 0.0f, 0.0f)), kEpsilon);
}

TEST(JoltMath, Product_AppliesRightOperandFirst_LikeMat4)
{
    const Mat4 a = Mat4::Translation(Vec3(10.0f, 0.0f, 0.0f));
    const Mat4 b = Mat4::Rotation(kRotation);
    ExpectMatNear(FromJolt(ToJolt(a) * ToJolt(b)), a * b);
    // Column vectors: (A * B) * p == A * (B * p), and translation applies last.
    const Vec3 p(1.0f, 2.0f, 3.0f);
    const Vec3 expected = Rotate(kRotation, p) + Vec3(10.0f, 0.0f, 0.0f);
    INTERI_EXPECT_VEC_NEAR(FromJolt((ToJolt(a) * ToJolt(b)) * ToJolt(p)), expected, kEpsilon);
}

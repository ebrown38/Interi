// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <cmath>

using interi::Mat4;
using interi::Quat;
using interi::Transform;
using interi::Vec3;
using interi::Vec4;

namespace
{
constexpr float kEps = 1e-5f;

void ExpectMatNear(const Mat4& a, const Mat4& b, float epsilon)
{
    for (int c = 0; c < 4; ++c)
    {
        INTERI_EXPECT_VEC_NEAR(a.columns[c], b.columns[c], epsilon) << "column " << c;
    }
}
} // namespace

// --- Vectors --------------------------------------------------------------------------------------------------------

TEST(Vec3, Cross_IsRightHanded)
{
    INTERI_EXPECT_VEC_NEAR(interi::Cross(Vec3::UnitX(), Vec3::UnitY()), Vec3::UnitZ(), 0.0f);
    INTERI_EXPECT_VEC_NEAR(interi::Cross(Vec3::UnitY(), Vec3::UnitZ()), Vec3::UnitX(), 0.0f);
}

TEST(Vec3, ArithmeticAndDot)
{
    const Vec3 a{1, 2, 3};
    const Vec3 b{4, -5, 6};
    INTERI_EXPECT_VEC_NEAR(a + b, Vec3(5, -3, 9), 0.0f);
    INTERI_EXPECT_VEC_NEAR(a * 2.0f, Vec3(2, 4, 6), 0.0f);
    INTERI_EXPECT_VEC_NEAR(-a, Vec3(-1, -2, -3), 0.0f);
    EXPECT_EQ(interi::Dot(a, b), 12.0f);
    EXPECT_EQ(interi::LengthSquared(a), 14.0f);
}

TEST(Vec3, Normalize_GivesUnitLength)
{
    const Vec3 n = interi::Normalize(Vec3{3, 0, 4});
    EXPECT_NEAR(interi::Length(n), 1.0f, kEps);
    INTERI_EXPECT_VEC_NEAR(n, Vec3(0.6f, 0.0f, 0.8f), kEps);
    INTERI_EXPECT_VEC_NEAR(interi::NormalizeOrZero(Vec3{}), Vec3{}, 0.0f);
}

TEST(Vec3, MinMaxLerp)
{
    INTERI_EXPECT_VEC_NEAR(interi::Min(Vec3{1, 5, 3}, Vec3{4, 2, 6}), Vec3(1, 2, 3), 0.0f);
    INTERI_EXPECT_VEC_NEAR(interi::Max(Vec3{1, 5, 3}, Vec3{4, 2, 6}), Vec3(4, 5, 6), 0.0f);
    INTERI_EXPECT_VEC_NEAR(interi::Lerp(Vec3{0, 0, 0}, Vec3{2, 4, 6}, 0.5f), Vec3(1, 2, 3), 0.0f);
}

TEST(Vec4, FromVec3_AndBack)
{
    const Vec4 v{Vec3{1, 2, 3}, 1.0f};
    EXPECT_EQ(v.w, 1.0f);
    INTERI_EXPECT_VEC_NEAR(v.XYZ(), Vec3(1, 2, 3), 0.0f);
    EXPECT_EQ(interi::Dot(v, Vec4{1, 1, 1, 1}), 7.0f);
}

// --- Quaternions ----------------------------------------------------------------------------------------------------

TEST(Quat, AxisAngle_FollowsRightHandRule)
{
    // Right-handed, Y up: +90 degrees about +Y turns +X into -Z.
    const Quat q = Quat::FromAxisAngle(Vec3::UnitY(), interi::kPi / 2);
    INTERI_EXPECT_VEC_NEAR(interi::Rotate(q, Vec3::UnitX()), Vec3(0, 0, -1), kEps);
    INTERI_EXPECT_VEC_NEAR(interi::Rotate(Quat::Identity(), Vec3(1, 2, 3)), Vec3(1, 2, 3), 0.0f);
}

TEST(Quat, Multiply_ComposesRightToLeft)
{
    const Quat a = Quat::FromAxisAngle(Vec3::UnitX(), 0.7f);
    const Quat b = Quat::FromAxisAngle(interi::Normalize(Vec3{1, 1, 0}), -1.3f);
    const Vec3 v{0.3f, -2.0f, 1.5f};
    INTERI_EXPECT_VEC_NEAR(interi::Rotate(a * b, v), interi::Rotate(a, interi::Rotate(b, v)), kEps);
}

TEST(Quat, Inverse_UndoesRotation)
{
    const Quat q = Quat::FromAxisAngle(interi::Normalize(Vec3{1, 2, 3}), 2.1f);
    INTERI_EXPECT_QUAT_NEAR(q * interi::Inverse(q), Quat::Identity(), kEps);
}

TEST(Quat, Slerp_Endpoints)
{
    const Quat a = Quat::FromAxisAngle(Vec3::UnitZ(), 0.2f);
    const Quat b = Quat::FromAxisAngle(Vec3::UnitZ(), 1.4f);
    INTERI_EXPECT_QUAT_NEAR(interi::Slerp(a, b, 0.0f), a, kEps);
    INTERI_EXPECT_QUAT_NEAR(interi::Slerp(a, b, 1.0f), b, kEps);
    INTERI_EXPECT_QUAT_NEAR(interi::Slerp(a, b, 0.5f), Quat::FromAxisAngle(Vec3::UnitZ(), 0.8f), kEps);
}

TEST(Quat, Slerp_TakesShortestPath)
{
    const Quat a = Quat::FromAxisAngle(Vec3::UnitY(), 0.1f);
    const Quat b = Quat::FromAxisAngle(Vec3::UnitY(), 0.3f);
    const Quat bNegated{-b.x, -b.y, -b.z, -b.w}; // same rotation, opposite hemisphere
    INTERI_EXPECT_QUAT_NEAR(interi::Slerp(a, bNegated, 0.5f), Quat::FromAxisAngle(Vec3::UnitY(), 0.2f), kEps);
}

TEST(Quat, Slerp_NearlyIdentical_StaysFinite)
{
    const Quat a = Quat::FromAxisAngle(Vec3::UnitX(), 1.0f);
    const Quat r = interi::Slerp(a, a, 0.5f);
    EXPECT_TRUE(std::isfinite(r.x) && std::isfinite(r.w));
    INTERI_EXPECT_QUAT_NEAR(r, a, kEps);
}

TEST(Quat, FromMat4_RoundTrips)
{
    for (const Quat q : {Quat::FromAxisAngle(Vec3::UnitX(), 3.0f), Quat::FromAxisAngle(Vec3::UnitY(), -2.5f),
                         Quat::FromAxisAngle(interi::Normalize(Vec3{-1, 2, 0.5f}), 1.1f), Quat::Identity()})
    {
        INTERI_EXPECT_QUAT_NEAR(Quat::FromRotationMatrix(Mat4::Rotation(q)), q, kEps);
    }
}

// --- Matrices -------------------------------------------------------------------------------------------------------

TEST(Mat4, Translation_LivesInColumn3)
{
    const Mat4 m = Mat4::Translation(Vec3{5, 6, 7});
    INTERI_EXPECT_VEC_NEAR(m.columns[3], Vec4(5, 6, 7, 1), 0.0f);
    EXPECT_EQ(m(0, 3), 5.0f) << "m(row, column)";
    INTERI_EXPECT_VEC_NEAR(interi::TransformPoint(m, Vec3{1, 1, 1}), Vec3(6, 7, 8), 0.0f);
    INTERI_EXPECT_VEC_NEAR(interi::TransformVector(m, Vec3{1, 1, 1}), Vec3(1, 1, 1), 0.0f) << "vectors ignore it";
}

TEST(Mat4, Multiply_AppliesRightOperandFirst)
{
    const Mat4 translate = Mat4::Translation(Vec3{10, 0, 0});
    const Mat4 rotate = Mat4::Rotation(Quat::FromAxisAngle(Vec3::UnitZ(), interi::kPi / 2));
    // Rotate +X to +Y, then translate.
    INTERI_EXPECT_VEC_NEAR(interi::TransformPoint(translate * rotate, Vec3::UnitX()), Vec3(10, 1, 0), kEps);
    INTERI_EXPECT_VEC_NEAR(interi::TransformPoint(rotate * translate, Vec3::UnitX()), Vec3(0, 11, 0), kEps);
}

TEST(Mat4, Identity_IsNeutral)
{
    const Mat4 m = Mat4::FromTRS(Vec3{1, 2, 3}, Quat::FromAxisAngle(Vec3::UnitX(), 0.5f), Vec3{2, 2, 2});
    ExpectMatNear(Mat4::Identity() * m, m, 0.0f);
    ExpectMatNear(m * Mat4::Identity(), m, 0.0f);
}

TEST(Mat4, RotationMatrix_MatchesQuaternionRotate)
{
    const Quat q = Quat::FromAxisAngle(interi::Normalize(Vec3{0.2f, -1, 0.4f}), 0.9f);
    const Vec3 v{1.5f, -0.5f, 2.0f};
    INTERI_EXPECT_VEC_NEAR(interi::TransformVector(Mat4::Rotation(q), v), interi::Rotate(q, v), kEps);
}

TEST(Mat4, Determinant_OfScaleIsProduct)
{
    EXPECT_NEAR(interi::Determinant(Mat4::Scale(Vec3{2, 3, 4})), 24.0f, kEps);
    EXPECT_NEAR(interi::Determinant(Mat4::Rotation(Quat::FromAxisAngle(Vec3::UnitY(), 1.0f))), 1.0f, kEps);
}

TEST(Mat4, Transpose_TwiceIsIdentity)
{
    const Mat4 m = Mat4::FromTRS(Vec3{1, 2, 3}, Quat::FromAxisAngle(Vec3::UnitX(), 0.5f), Vec3{1, 2, 3});
    ExpectMatNear(interi::Transpose(interi::Transpose(m)), m, 0.0f);
    EXPECT_EQ(interi::Transpose(m)(0, 3), m(3, 0));
}

class Mat4Random : public interi::testing::SeededTest
{
protected:
    Mat4 RandomTRS()
    {
        const Vec3 axis = interi::Normalize(Vec3{Rng().NextFloat(-1, 1), Rng().NextFloat(-1, 1), 0.5f});
        return Mat4::FromTRS(Vec3{Rng().NextFloat(-10, 10), Rng().NextFloat(-10, 10), Rng().NextFloat(-10, 10)},
                             Quat::FromAxisAngle(axis, Rng().NextFloat(-3, 3)),
                             Vec3{Rng().NextFloat(0.5f, 2), Rng().NextFloat(0.5f, 2), Rng().NextFloat(0.5f, 2)});
    }
};

TEST_F(Mat4Random, Inverse_TimesOriginal_IsIdentity)
{
    for (int i = 0; i < 100; ++i)
    {
        const Mat4 m = RandomTRS();
        ExpectMatNear(m * interi::Inverse(m), Mat4::Identity(), 1e-4f);
        ExpectMatNear(interi::Inverse(m) * m, Mat4::Identity(), 1e-4f);
    }
}

TEST_F(Mat4Random, Inverse_OfGeneralProjectiveMatrix)
{
    // Not affine: the bottom row is not (0, 0, 0, 1).
    Mat4 m = RandomTRS();
    m.columns[0].w = 0.25f;
    m.columns[2].w = -0.5f;
    ExpectMatNear(m * interi::Inverse(m), Mat4::Identity(), 1e-4f);
}

TEST_F(Mat4Random, Multiply_IsAssociative)
{
    const Mat4 a = RandomTRS();
    const Mat4 b = RandomTRS();
    const Vec3 p{1, -2, 3};
    INTERI_EXPECT_VEC_NEAR(interi::TransformPoint(a * b, p), interi::TransformPoint(a, interi::TransformPoint(b, p)),
                           1e-3f);
}

TEST(Mat4, TryInverse_SingularReturnsNothing)
{
    EXPECT_FALSE(interi::TryInverse(Mat4::Scale(Vec3{1, 0, 1})).has_value());
    EXPECT_TRUE(interi::TryInverse(Mat4::Scale(Vec3{1, 2, 1})).has_value());
}

// --- Transform ------------------------------------------------------------------------------------------------------

TEST(Transform, ToMat4_MatchesTransformPoint)
{
    const Transform t{.position = {1, 2, 3}, .rotation = Quat::FromAxisAngle(Vec3::UnitZ(), 0.6f), .scale = {2, 2, 2}};
    const Vec3 p{0.5f, -1, 4};
    INTERI_EXPECT_VEC_NEAR(t.TransformPoint(p), interi::TransformPoint(t.ToMat4(), p), kEps);
}

TEST(Transform, Compose_MatchesMatrixProduct)
{
    const Transform parent{
        .position = {1, 0, 0}, .rotation = Quat::FromAxisAngle(Vec3::UnitY(), 1.0f), .scale = {2, 2, 2}};
    const Transform child{
        .position = {0, 3, 0}, .rotation = Quat::FromAxisAngle(Vec3::UnitX(), -0.4f), .scale = {0.5f, 0.5f, 0.5f}};
    const Transform world = parent * child;
    ExpectMatNear(world.ToMat4(), parent.ToMat4() * child.ToMat4(), 1e-5f);
}

TEST(Transform, Inverse_UndoesUniformScaleTransform)
{
    const Transform t{.position = {4, -2, 1}, .rotation = Quat::FromAxisAngle(Vec3::UnitZ(), 2.0f), .scale = {3, 3, 3}};
    const Vec3 p{1, 2, 3};
    INTERI_EXPECT_VEC_NEAR(t.Inverse().TransformPoint(t.TransformPoint(p)), p, 1e-5f);
}

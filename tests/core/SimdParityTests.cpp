// SPDX-License-Identifier: MIT
#include <interi/core/math/MathKernels.h>
#include <interi/core/math/Quat.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <vector>

// The SIMD kernels must agree with the scalar reference kernels, which every build compiles. Operations whose
// SIMD and scalar code perform the same float operations in the same order are compared bit-exactly.

#if INTERI_HAS_SIMD_KERNELS

namespace scalar = interi::math::scalar;
namespace simd = interi::math::simd;
using interi::Mat4;
using interi::Quat;
using interi::Vec3;
using interi::Vec4;

namespace
{
class SimdParity : public interi::testing::SeededTest
{
protected:
    float F() { return Rng().NextFloat(-4.0f, 4.0f); }
    Vec3 RandomVec3() { return {F(), F(), F()}; }
    Vec4 RandomVec4() { return {F(), F(), F(), F()}; }
    Mat4 RandomMat4() { return Mat4{{RandomVec4(), RandomVec4(), RandomVec4(), RandomVec4()}}; }
    Quat RandomQuat()
    {
        const Vec4 v = RandomVec4();
        return interi::Normalize(Quat{v.x, v.y, v.z, v.w});
    }
};

constexpr int kIterations = 1000;

void ExpectMatEqual(const Mat4& a, const Mat4& b)
{
    for (int c = 0; c < 4; ++c)
    {
        INTERI_EXPECT_VEC_NEAR(a.columns[c], b.columns[c], 0.0f);
    }
}
} // namespace

TEST_F(SimdParity, Mat4Multiply_BitExact)
{
    for (int i = 0; i < kIterations; ++i)
    {
        const Mat4 a = RandomMat4();
        const Mat4 b = RandomMat4();
        ExpectMatEqual(simd::Multiply(a, b), scalar::Multiply(a, b));
    }
}

TEST_F(SimdParity, Mat4TimesVec4_BitExact)
{
    for (int i = 0; i < kIterations; ++i)
    {
        const Mat4 m = RandomMat4();
        const Vec4 v = RandomVec4();
        INTERI_EXPECT_VEC_NEAR(simd::Multiply(m, v), scalar::Multiply(m, v), 0.0f);
    }
}

TEST_F(SimdParity, Mat4Inverse_Matches)
{
    for (int i = 0; i < kIterations; ++i)
    {
        const Mat4 m = RandomMat4();
        if (std::abs(scalar::Determinant(m)) < 1.0f)
        {
            continue; // poorly conditioned: both are allowed to lose precision differently
        }
        EXPECT_NEAR(simd::Determinant(m), scalar::Determinant(m), 1e-3f * std::abs(scalar::Determinant(m)));
        const Mat4 a = simd::Inverse(m);
        const Mat4 b = scalar::Inverse(m);
        for (int c = 0; c < 4; ++c)
        {
            INTERI_EXPECT_VEC_NEAR(a.columns[c], b.columns[c], 1e-4f);
        }
    }
}

TEST_F(SimdParity, QuatMultiply_Matches)
{
    for (int i = 0; i < kIterations; ++i)
    {
        const Quat a = RandomQuat();
        const Quat b = RandomQuat();
        INTERI_EXPECT_QUAT_NEAR(simd::Multiply(a, b), scalar::Multiply(a, b), 1e-6f);
    }
}

TEST_F(SimdParity, QuatRotate_Matches)
{
    for (int i = 0; i < kIterations; ++i)
    {
        const Quat q = RandomQuat();
        const Vec3 v = RandomVec3();
        INTERI_EXPECT_VEC_NEAR(simd::Rotate(q, v), scalar::Rotate(q, v), 1e-5f);
    }
}

TEST_F(SimdParity, TransformPoints_BitExactAndWritesOnlyOutputRange)
{
    const Mat4 m = RandomMat4();
    std::vector<Vec3> input(37);
    for (Vec3& p : input)
    {
        p = RandomVec3();
    }
    // A guard element after the output range catches 16-byte stores that overrun the last Vec3.
    std::vector<Vec3> simdOut(input.size() + 1, Vec3{-7, -7, -7});
    std::vector<Vec3> scalarOut(input.size());
    simd::TransformPoints(m, input, std::span(simdOut).first(input.size()));
    scalar::TransformPoints(m, input, scalarOut);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        INTERI_EXPECT_VEC_NEAR(simdOut[i], scalarOut[i], 0.0f) << i;
    }
    INTERI_EXPECT_VEC_NEAR(simdOut.back(), Vec3(-7, -7, -7), 0.0f);
}

#else

TEST(SimdParity, ScalarOnlyBuild)
{
    GTEST_SKIP() << "built without SIMD kernels (INTERI_SIMD=OFF or non-x86 target)";
}

#endif

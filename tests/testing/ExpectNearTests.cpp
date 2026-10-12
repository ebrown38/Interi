// SPDX-License-Identifier: MIT
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
struct TestVec3
{
    float x, y, z;
};

struct TestQuat
{
    float x, y, z, w;
};
} // namespace

using interi::testing::QuatNear;
using interi::testing::VecNear;

TEST(VecNear, WithinEpsilon_Succeeds)
{
    EXPECT_TRUE(VecNear(TestVec3{1.0f, 2.0f, 3.0f}, TestVec3{1.0005f, 1.9995f, 3.0f}, 1e-3f));
    INTERI_EXPECT_VEC_NEAR((TestVec3{0.0f, 0.0f, 0.0f}), (TestVec3{0.0f, 0.0f, 1e-6f}), 1e-5f);
}

TEST(VecNear, OutsideEpsilon_FailsWithComponentInMessage)
{
    const ::testing::AssertionResult result = VecNear(TestVec3{1.0f, 2.0f, 3.0f}, TestVec3{1.0f, 2.5f, 3.0f}, 1e-3f);
    EXPECT_FALSE(result);
    EXPECT_NE(std::string(result.message()).find("y"), std::string::npos) << result.message();
}

TEST(VecNear, NaN_Fails)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(VecNear(TestVec3{nan, 0.0f, 0.0f}, TestVec3{nan, 0.0f, 0.0f}, 1.0f));
}

TEST(VecNear, FourComponents_ComparesW)
{
    EXPECT_FALSE(VecNear(TestQuat{0, 0, 0, 1}, TestQuat{0, 0, 0, 2}, 1e-3f));
}

TEST(QuatNear, SameRotation_Succeeds)
{
    INTERI_EXPECT_QUAT_NEAR((TestQuat{0.0f, 0.7071068f, 0.0f, 0.7071068f}),
                            (TestQuat{0.0f, 0.7071070f, 0.0f, 0.7071066f}), 1e-5f);
}

TEST(QuatNear, NegatedQuaternion_TreatedAsSameRotation)
{
    EXPECT_TRUE(QuatNear(TestQuat{0.1f, 0.2f, 0.3f, 0.927362f}, TestQuat{-0.1f, -0.2f, -0.3f, -0.927362f}, 1e-5f));
}

TEST(QuatNear, DifferentRotation_Fails)
{
    EXPECT_FALSE(QuatNear(TestQuat{0, 0, 0, 1}, TestQuat{1, 0, 0, 0}, 1e-3f));
}

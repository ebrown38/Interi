// SPDX-License-Identifier: MIT
#include <interi/gameplay/FollowCamera.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <numbers>

using interi::Vec3;
using interi::gameplay::FollowCamera;

TEST(FollowCamera, YawZero_LooksAlongMinusZ)
{
    FollowCamera camera;
    camera.pitch = 0.0f;
    INTERI_EXPECT_VEC_NEAR(camera.Forward(), Vec3(0.0f, 0.0f, -1.0f), 1e-6f);
    INTERI_EXPECT_VEC_NEAR(camera.Right(), Vec3(1.0f, 0.0f, 0.0f), 1e-6f);
}

TEST(FollowCamera, PositiveYaw_TurnsLeft)
{
    FollowCamera camera;
    camera.yaw = std::numbers::pi_v<float> / 2.0f;
    INTERI_EXPECT_VEC_NEAR(camera.FlatForward(), Vec3(-1.0f, 0.0f, 0.0f), 1e-6f);
}

TEST(FollowCamera, MouseRight_TurnsRight_MouseDown_LooksDown)
{
    FollowCamera camera;
    camera.pitch = 0.0f;
    camera.Rotate(100.0f, 0.0f);
    EXPECT_GT(camera.FlatForward().x, 0.0f);
    camera.Rotate(0.0f, 100.0f);
    EXPECT_LT(camera.pitch, 0.0f);
}

TEST(FollowCamera, Pitch_Clamped)
{
    FollowCamera camera;
    camera.Rotate(0.0f, 1e6f);
    EXPECT_EQ(camera.pitch, camera.minPitch);
    camera.Rotate(0.0f, -1e6f);
    EXPECT_EQ(camera.pitch, camera.maxPitch);
}

TEST(FollowCamera, Eye_BehindAndRightOfTarget)
{
    FollowCamera camera;
    camera.pitch = 0.0f;
    const Vec3 target(1.0f, 2.0f, 3.0f);
    INTERI_EXPECT_VEC_NEAR(camera.Eye(target), Vec3(1.0f + camera.shoulder, 2.0f, 3.0f + camera.distance), 1e-5f);
}

TEST(FollowCamera, MoveDirection_FollowsYaw)
{
    FollowCamera camera;
    camera.yaw = std::numbers::pi_v<float>; // looking along +Z
    INTERI_EXPECT_VEC_NEAR(camera.MoveDirection(0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f), 1e-6f);
    INTERI_EXPECT_VEC_NEAR(camera.MoveDirection(1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 1e-6f);
}

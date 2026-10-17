// SPDX-License-Identifier: MIT
#include <interi/editor/EditorCamera.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

TEST(OrbitCamera, Target_ProjectsToCentre)
{
    OrbitCamera camera;
    const auto centre = camera.Project(camera.target, 800.0f, 600.0f);
    ASSERT_TRUE(centre.has_value());
    EXPECT_NEAR(centre->x, 400.0f, 1e-2f);
    EXPECT_NEAR(centre->y, 300.0f, 1e-2f);
    EXPECT_NEAR(Length(camera.Eye() - camera.target), camera.distance, 1e-4f);
}

TEST(OrbitCamera, ScreenRay_ProjectsBackToPixel)
{
    OrbitCamera camera;
    camera.yaw = -0.8f;
    camera.pitch = 0.3f;
    for (const ScreenPoint pixel : {ScreenPoint{10, 20}, ScreenPoint{700, 550}, ScreenPoint{400, 300}})
    {
        const Ray ray = camera.ScreenRay(pixel, 800.0f, 600.0f);
        EXPECT_NEAR(Length(ray.direction), 1.0f, 1e-5f);
        const auto projected = camera.Project(ray.origin + ray.direction * 7.0f, 800.0f, 600.0f);
        ASSERT_TRUE(projected.has_value());
        EXPECT_NEAR(projected->x, pixel.x, 0.05f);
        EXPECT_NEAR(projected->y, pixel.y, 0.05f);
    }
    // Up in the viewport is up in the world.
    EXPECT_GT(camera.ScreenRay({400, 0}, 800, 600).direction.y, camera.ScreenRay({400, 600}, 800, 600).direction.y);
}

TEST(OrbitCamera, BehindEye_NotProjected)
{
    OrbitCamera camera;
    const Vec3 behind = camera.Eye() + (camera.Eye() - camera.target);
    EXPECT_FALSE(camera.Project(behind, 800.0f, 600.0f).has_value());
}

TEST(OrbitCamera, OrbitPanZoom_StayInRange)
{
    OrbitCamera camera;
    camera.Orbit(0.0f, 1.0e6f);
    EXPECT_LE(camera.pitch, 1.5f);
    camera.Zoom(1000.0f);
    EXPECT_GE(camera.distance, 0.5f);
    const Vec3 before = camera.target;
    camera.Pan(100.0f, 0.0f, 600.0f);
    EXPECT_GT(Length(camera.target - before), 0.0f);
    EXPECT_NEAR(camera.target.y, before.y, 1e-3f); // horizontal pan at this pitch moves sideways only
}

// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/Gizmo.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <numbers>

using namespace interi;
using namespace interi::editor;

namespace
{

/// A ray from far along +Z (and a bit up/right) aimed at `target`.
Ray RayTowards(const Vec3& target, const Vec3& from = {0.3f, 0.4f, 10.0f})
{
    return {.origin = from, .direction = Normalize(target - from)};
}

} // namespace

TEST(Snap, GridSize_RoundsCorrectly)
{
    EXPECT_FLOAT_EQ(Snap(0.74f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(Snap(0.76f, 0.5f), 1.0f);
    EXPECT_FLOAT_EQ(Snap(-0.76f, 0.5f), -1.0f);
    EXPECT_FLOAT_EQ(Snap(0.25f, 0.5f), 0.5f); // halfway: away from zero
    EXPECT_FLOAT_EQ(Snap(-0.25f, 0.5f), -0.5f);
    EXPECT_FLOAT_EQ(Snap(3.3f, 1.0f), 3.0f);
    EXPECT_FLOAT_EQ(Snap(3.3f, 0.0f), 3.3f); // no grid
    EXPECT_FLOAT_EQ(Snap(3.3f, -1.0f), 3.3f);
    EXPECT_FLOAT_EQ(Snap(37.0f, 15.0f), 30.0f); // angle steps in degrees work the same way
    INTERI_EXPECT_VEC_NEAR(Snap(Vec3(1.26f, -0.1f, 7.9f), 0.25f), Vec3(1.25f, 0.0f, 8.0f), 1e-6f);
}

TEST(Gizmo, GizmoRayPick_SelectsNearestAxis)
{
    const Gizmo gizmo{.mode = GizmoMode::Translate, .origin = {0, 0, 0}, .size = 1.0f};
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.6f, 0.0f, 0.0f})), GizmoAxis::X);
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.0f, 0.7f, 0.0f})), GizmoAxis::Y);
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.0f, 0.0f, 0.5f}, {3.0f, 0.2f, 0.4f})), GizmoAxis::Z);
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({2.0f, 2.0f, 0.0f})), GizmoAxis::None); // past the handles
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.5f, 0.5f, 0.0f})), GizmoAxis::None); // between them
    // Near both X and Y (close to the origin): the nearer one wins.
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.3f, 0.05f, 0.0f})), GizmoAxis::X);
    EXPECT_EQ(GizmoRayPick(gizmo, RayTowards({0.05f, 0.3f, 0.0f})), GizmoAxis::Y);
}

TEST(Gizmo, RotateRings_Picked)
{
    const Gizmo gizmo{.mode = GizmoMode::Rotate, .origin = {0, 0, 0}, .size = 1.0f};
    // The Z ring lies in the XY plane: a ray down -Z through (1, 0) hits it; (0.5, 0) is inside it.
    EXPECT_EQ(GizmoRayPick(gizmo, {.origin = {0.0f, 1.0f, 10.0f}, .direction = {0, 0, -1}}), GizmoAxis::Z);
    EXPECT_EQ(GizmoRayPick(gizmo, {.origin = {0.5f, 0.3f, 10.0f}, .direction = {0, 0, -1}}), GizmoAxis::None);
}

TEST(Gizmo, TranslateDrag_DistanceAlongAxis)
{
    const Gizmo gizmo{.mode = GizmoMode::Translate, .origin = {1, 0, 0}, .size = 1.0f};
    const auto drag = GizmoDrag::Begin(gizmo, GizmoAxis::X, RayTowards({1.5f, 0.0f, 0.0f}));
    ASSERT_TRUE(drag.has_value());
    EXPECT_NEAR(drag->Distance(RayTowards({3.5f, 0.0f, 0.0f})), 2.0f, 1e-4f);
    EXPECT_NEAR(drag->Distance(RayTowards({0.5f, 0.0f, 0.0f})), -1.0f, 1e-4f);
    // Parallel to the axis: can't grab.
    EXPECT_FALSE(GizmoDrag::Begin(gizmo, GizmoAxis::X, {.origin = {-5, 0, 0}, .direction = {1, 0, 0}}).has_value());
}

TEST(Gizmo, RotateDrag_AngleAboutAxis)
{
    const Gizmo gizmo{.mode = GizmoMode::Rotate, .origin = {0, 0, 0}, .size = 1.0f};
    const Ray down = {.origin = {1.0f, 10.0f, 0.0f}, .direction = {0, -1, 0}};
    const auto drag = GizmoDrag::Begin(gizmo, GizmoAxis::Y, down);
    ASSERT_TRUE(drag.has_value());
    // From +X to -Z is a quarter turn counter-clockwise looking down +Y.
    EXPECT_NEAR(drag->Angle({.origin = {0.0f, 10.0f, -1.0f}, .direction = {0, -1, 0}}),
                std::numbers::pi_v<float> / 2.0f, 1e-5f);
    // Same convention as Quat::FromAxisAngle: rotating +X by that angle about +Y gives -Z.
    INTERI_EXPECT_VEC_NEAR(Rotate(Quat::FromAxisAngle({0, 1, 0}, std::numbers::pi_v<float> / 2.0f), Vec3(1, 0, 0)),
                           Vec3(0, 0, -1), 1e-6f);
}

TEST(Gizmo, ScaleDrag_FactorFromHandleLength)
{
    const Gizmo gizmo{.mode = GizmoMode::Scale, .origin = {0, 0, 0}, .size = 1.0f};
    const auto drag = GizmoDrag::Begin(gizmo, GizmoAxis::Y, RayTowards({0.0f, 0.5f, 0.0f}));
    ASSERT_TRUE(drag.has_value());
    EXPECT_NEAR(drag->Factor(RayTowards({0.0f, 1.5f, 0.0f})), 3.0f, 1e-4f);
}

TEST(RayBox, HitsAndMisses)
{
    const Ray ray{.origin = {0, 0, 10}, .direction = {0, 0, -1}};
    const auto hit = RayBox(ray, {0, 0, 0}, Quat(), {1, 1, 1});
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 9.0f, 1e-5f);
    EXPECT_FALSE(RayBox(ray, {3, 0, 0}, Quat(), {1, 1, 1}).has_value());
    // A long box rotated a quarter turn about Y now lies along Z and is hit at its far end.
    const auto rotated = RayBox({.origin = {0, 0, 10}, .direction = {0, 0, -1}}, {0, 0, 0},
                                Quat::FromAxisAngle({0, 1, 0}, std::numbers::pi_v<float> / 2.0f), {4, 1, 1});
    ASSERT_TRUE(rotated.has_value());
    EXPECT_NEAR(*rotated, 6.0f, 1e-4f);
    EXPECT_EQ(RayBox({.origin = {0, 0, 0}, .direction = {1, 0, 0}}, {0, 0, 0}, Quat(), {1, 1, 1}), 0.0f); // inside
    const auto ground = RayPlaneY({.origin = {0, 5, 0}, .direction = Normalize(Vec3(1, -1, 0))}, 0.0f);
    ASSERT_TRUE(ground.has_value());
    EXPECT_NEAR(*ground, 5.0f * std::numbers::sqrt2_v<float>, 1e-4f);
    EXPECT_FALSE(RayPlaneY({.origin = {0, 5, 0}, .direction = {0, 1, 0}}, 0.0f).has_value());
}

// SPDX-License-Identifier: MIT
#include <interi/physics/Components.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
// clang-format on

#include <gtest/gtest.h>

#include <array>

using interi::ErrorCode;
using interi::Quat;
using interi::Vec3;
using interi::physics::ColliderComponent;
using interi::physics::ColliderPart;

TEST(Collider, Box_HasHalfExtents)
{
    const ColliderComponent box = ColliderComponent::Box(Vec3(1.0f, 2.0f, 3.0f));
    ASSERT_NE(box.shape, nullptr);
    EXPECT_EQ(box.shape->GetSubType(), JPH::EShapeSubType::Box);
    EXPECT_NEAR(box.shape->GetVolume(), 2.0f * 4.0f * 6.0f, 1e-3f);
}

TEST(Collider, ThinBox_ConvexRadiusClamped)
{
    // Jolt rejects a convex radius larger than a half extent; thin slabs are common in cooked debris.
    const ColliderComponent slab = ColliderComponent::Box(Vec3(1.0f, 0.01f, 1.0f));
    ASSERT_NE(slab.shape, nullptr);
    EXPECT_NEAR(slab.shape->GetVolume(), 2.0f * 0.02f * 2.0f, 1e-4f);
}

TEST(Collider, ConvexHull_CubePoints_Volume)
{
    std::array<Vec3, 8> points;
    for (int i = 0; i < 8; ++i)
    {
        points[static_cast<std::size_t>(i)] =
            Vec3((i & 1) != 0 ? 0.5f : -0.5f, (i & 2) != 0 ? 0.5f : -0.5f, (i & 4) != 0 ? 0.5f : -0.5f);
    }
    const auto hull = ColliderComponent::ConvexHull(points);
    ASSERT_TRUE(hull.has_value()) << hull.error().message;
    EXPECT_EQ(hull->shape->GetSubType(), JPH::EShapeSubType::ConvexHull);
    EXPECT_NEAR(hull->shape->GetVolume(), 1.0f, 1e-3f);
}

TEST(Collider, ConvexHull_Collinear_Degenerate)
{
    // (Jolt accepts coplanar points: it builds a flat hull rounded by its convex radius.)
    const std::array<Vec3, 3> points = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0)};
    const auto hull = ColliderComponent::ConvexHull(points);
    ASSERT_FALSE(hull.has_value());
    EXPECT_EQ(hull.error().code, ErrorCode::Degenerate);
}

TEST(Collider, Compound_TwoBoxes_VolumeSums)
{
    const std::array<ColliderPart, 2> parts = {
        ColliderPart{.position = Vec3(-1.0f, 0.0f, 0.0f),
                     .rotation = Quat::Identity(),
                     .collider = ColliderComponent::Box(Vec3(0.5f))},
        ColliderPart{.position = Vec3(1.0f, 0.0f, 0.0f),
                     .rotation = Quat::Identity(),
                     .collider = ColliderComponent::Box(Vec3(0.5f))},
    };
    const auto compound = ColliderComponent::Compound(parts);
    ASSERT_TRUE(compound.has_value()) << compound.error().message;
    EXPECT_EQ(compound->shape->GetSubType(), JPH::EShapeSubType::StaticCompound);
    EXPECT_NEAR(compound->shape->GetVolume(), 2.0f, 1e-3f);
}

TEST(Collider, Compound_Empty_InvalidArgument)
{
    const auto compound = ColliderComponent::Compound({});
    ASSERT_FALSE(compound.has_value());
    EXPECT_EQ(compound.error().code, ErrorCode::InvalidArgument);
}

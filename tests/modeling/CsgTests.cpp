// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/modeling/Csg.h>
#include <interi/scene/Primitives.h>

#include <gtest/gtest.h>

#include <numbers>

using namespace interi;
using namespace interi::modeling;

namespace
{

scene::TriangleMesh Box(const Vec3& size)
{
    scene::PrimitiveParams params;
    params.size = size;
    return *scene::GeneratePrimitive(params);
}

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

} // namespace

TEST(Csg, UnionOfOverlappingBoxes_HasTheCombinedVolume)
{
    const scene::TriangleMesh a = Box(Vec3(2.0f));
    const scene::TriangleMesh b = Box(Vec3(2.0f));
    const CsgOperand operands[] = {{.mesh = &a, .transform = At({0, 0, 0})}, {.mesh = &b, .transform = At({1, 0, 0})}};
    auto result = Combine(operands, CsgOperation::Union);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(scene::IsClosedManifold(*result));
    EXPECT_NEAR(scene::MeshVolume(*result), 12.0, 1e-4); // 8 + 8 - 4 overlap
    const scene::MeshBounds bounds = scene::BoundsOf(*result);
    EXPECT_FLOAT_EQ(bounds.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 2.0f);
    EXPECT_TRUE(scene::ValidateMesh(*result).has_value());
}

TEST(Csg, Subtract_RemovesTheOverlapAndKeepsSlotsByOperand)
{
    const scene::TriangleMesh a = Box(Vec3(2.0f));
    const scene::TriangleMesh b = Box(Vec3(2.0f));
    const CsgOperand operands[] = {{.mesh = &a, .transform = At({0, 0, 0})}, {.mesh = &b, .transform = At({1, 0, 0})}};
    auto result = Combine(operands, CsgOperation::Subtract);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(scene::IsClosedManifold(*result));
    EXPECT_NEAR(scene::MeshVolume(*result), 4.0, 1e-4);
    // The cut face comes from the second box (slots 6..11); the rest keep the first box's (0..5).
    EXPECT_GE(result->SlotCount(), 7u);
    EXPECT_TRUE(std::ranges::any_of(result->slots, [](std::uint32_t slot) { return slot >= 6; }));
    EXPECT_TRUE(std::ranges::any_of(result->slots, [](std::uint32_t slot) { return slot < 6; }));
}

TEST(Csg, IntersectWithRotatedBox_IsInsideBoth)
{
    const scene::TriangleMesh a = Box(Vec3(2.0f));
    const scene::TriangleMesh b = Box(Vec3(2.0f));
    Transform rotated;
    rotated.rotation = Quat::FromAxisAngle({0, 1, 0}, std::numbers::pi_v<float> / 4.0f);
    const CsgOperand operands[] = {{.mesh = &a, .transform = {}}, {.mesh = &b, .transform = rotated}};
    auto result = Combine(operands, CsgOperation::Intersect);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(scene::IsClosedManifold(*result));
    // A square and the same square turned 45 degrees overlap in a regular octagon: 8 (sqrt 2 - 1) per unit height.
    EXPECT_NEAR(scene::MeshVolume(*result), 2.0 * 8.0 * (std::numbers::sqrt2 - 1.0), 1e-3);
}

TEST(Csg, MirroredOperand_StaysOutwardFacing)
{
    const scene::TriangleMesh a = Box(Vec3(2.0f));
    const scene::TriangleMesh b = Box(Vec3(2.0f));
    Transform mirrored = At({1, 0, 0});
    mirrored.scale = {-1.0f, 1.0f, 1.0f};
    const CsgOperand operands[] = {{.mesh = &a, .transform = {}}, {.mesh = &b, .transform = mirrored}};
    auto result = Combine(operands, CsgOperation::Union);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_NEAR(scene::MeshVolume(*result), 12.0, 1e-4);
}

TEST(Csg, InvalidInput_Fails)
{
    const scene::TriangleMesh a = Box(Vec3(2.0f));
    const CsgOperand one[] = {{.mesh = &a, .transform = {}}};
    EXPECT_FALSE(Combine(one, CsgOperation::Union).has_value());

    scene::PrimitiveParams plane;
    plane.shape = scene::PrimitiveShape::Plane;
    const scene::TriangleMesh open = *scene::GeneratePrimitive(plane);
    const CsgOperand withOpen[] = {{.mesh = &a, .transform = {}}, {.mesh = &open, .transform = {}}};
    auto result = Combine(withOpen, CsgOperation::Union);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);

    const CsgOperand apart[] = {{.mesh = &a, .transform = {}}, {.mesh = &a, .transform = At({10, 0, 0})}};
    auto empty = Combine(apart, CsgOperation::Intersect);
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, ErrorCode::Degenerate);
}

// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/Shadows.h>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace interi;
using namespace interi::render;

namespace
{

constexpr float kFov = std::numbers::pi_v<float> / 3.0f;

Vec3 Project(const Mat4& m, Vec3 point)
{
    const Vec4 clip = m * Vec4(point, 1.0f);
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

/// World-space corners of the camera frustum between two view distances.
std::array<Vec3, 8> SliceCorners(const Mat4& view, float aspect, float nearDistance, float farDistance)
{
    const Mat4 cameraToWorld = Inverse(view);
    std::array<Vec3, 8> corners{};
    std::size_t i = 0;
    for (const float d : {nearDistance, farDistance})
    {
        const float halfHeight = d * std::tan(kFov * 0.5f);
        const float halfWidth = halfHeight * aspect;
        for (const float sx : {-1.0f, 1.0f})
        {
            for (const float sy : {-1.0f, 1.0f})
            {
                const Vec4 world = cameraToWorld * Vec4(sx * halfWidth, sy * halfHeight, -d, 1.0f);
                corners[i++] = {world.x, world.y, world.z};
            }
        }
    }
    return corners;
}

} // namespace

TEST(Shadows, CascadeSplits_EndAtNearAndFarAndIncrease)
{
    const auto splits = CascadeSplits(0.1f, 100.0f, 4, 0.75f);
    EXPECT_FLOAT_EQ(splits[0], 0.1f);
    EXPECT_FLOAT_EQ(splits[4], 100.0f);
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_LT(splits[i], splits[i + 1]);
    }
}

TEST(Shadows, CascadeSplits_LambdaBlendsUniformAndLogarithmic)
{
    const auto uniform = CascadeSplits(1.0f, 81.0f, 4, 0.0f);
    EXPECT_NEAR(uniform[1], 21.0f, 1e-4f);
    EXPECT_NEAR(uniform[2], 41.0f, 1e-4f);
    const auto logarithmic = CascadeSplits(1.0f, 81.0f, 4, 1.0f);
    EXPECT_NEAR(logarithmic[1], 3.0f, 1e-4f);
    EXPECT_NEAR(logarithmic[2], 9.0f, 1e-4f);
    EXPECT_NEAR(logarithmic[3], 27.0f, 1e-3f);
}

TEST(Shadows, FitCascade_ContainsTheFrustumSlice)
{
    const Mat4 view = LookAt({4, 3, 6}, {0, 0, 0}, {0, 1, 0});
    const Vec3 light = Normalize(Vec3{-0.4f, -1.0f, -0.3f});
    const CascadeFit fit = FitCascade(view, kFov, 1.5f, 2.0f, 15.0f, light, 1024);
    for (const Vec3& corner : SliceCorners(view, 1.5f, 2.0f, 15.0f))
    {
        const Vec3 ndc = Project(fit.viewProjection, corner);
        EXPECT_GE(ndc.x, -1.0f);
        EXPECT_LE(ndc.x, 1.0f);
        EXPECT_GE(ndc.y, -1.0f);
        EXPECT_LE(ndc.y, 1.0f);
        EXPECT_GT(ndc.z, 0.0f);
        EXPECT_LT(ndc.z, 1.0f);
    }
}

TEST(Shadows, FitCascade_NearerToTheLightHasGreaterDepth)
{
    const Mat4 view = LookAt({4, 3, 6}, {0, 0, 0}, {0, 1, 0});
    const Vec3 light{0.0f, -1.0f, 0.0f};
    const CascadeFit fit = FitCascade(view, kFov, 1.0f, 1.0f, 10.0f, light, 512);
    // Reverse-Z, like the camera: a point above (closer to a light shining down) has greater depth.
    EXPECT_GT(Project(fit.viewProjection, {0, 1, 0}).z, Project(fit.viewProjection, {0, 0, 0}).z);
}

TEST(Shadows, FitCascade_CameraTranslationMovesShadowMapByWholeTexels)
{
    // Shadow edges shimmer when the shadow map moves by fractions of a texel as the camera moves. Snapping keeps
    // the projection of any fixed world point at the same sub-texel position.
    const Vec3 light = Normalize(Vec3{-0.4f, -1.0f, -0.3f});
    constexpr std::uint32_t kMapSize = 1024;
    const auto texelPhase = [&](Vec3 eye)
    {
        const Mat4 view = LookAt(eye, eye + Vec3{-0.5f, -0.3f, -1.0f}, {0, 1, 0});
        const CascadeFit fit = FitCascade(view, kFov, 1.0f, 1.0f, 20.0f, light, kMapSize);
        const Vec3 ndc = Project(fit.viewProjection, {1.234f, 0.5f, -2.0f});
        const float texelsX = (ndc.x * 0.5f + 0.5f) * kMapSize;
        const float texelsY = (ndc.y * 0.5f + 0.5f) * kMapSize;
        return Vec3{texelsX - std::floor(texelsX), texelsY - std::floor(texelsY), 0.0f};
    };
    const Vec3 a = texelPhase({5.0f, 4.0f, 8.0f});
    const Vec3 b = texelPhase({5.013f, 4.0f, 8.021f}); // a fraction of a texel away
    EXPECT_NEAR(a.x, b.x, 2e-3f);
    EXPECT_NEAR(a.y, b.y, 2e-3f);
}

TEST(Camera, OrthographicReverseZ_NearIsOneFarIsZero)
{
    const Mat4 ortho = OrthographicReverseZ(-1, 1, -1, 1, 2.0f, 10.0f);
    EXPECT_NEAR(Project(ortho, {0, 0, -2.0f}).z, 1.0f, 1e-6f);
    EXPECT_NEAR(Project(ortho, {0, 0, -10.0f}).z, 0.0f, 1e-6f);
}

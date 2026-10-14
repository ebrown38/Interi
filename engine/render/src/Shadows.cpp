// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/Shadows.h>

#include <algorithm>
#include <cmath>

namespace interi::render
{

std::array<float, kMaxShadowCascades + 1> CascadeSplits(float nearPlane, float farPlane, std::uint32_t count,
                                                        float lambda) noexcept
{
    INTERI_ASSERT(count >= 1 && count <= kMaxShadowCascades && nearPlane > 0.0f && farPlane > nearPlane);
    std::array<float, kMaxShadowCascades + 1> splits{};
    splits.fill(farPlane);
    splits[0] = nearPlane;
    for (std::uint32_t i = 1; i < count; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, t);
        const float uniform = nearPlane + (farPlane - nearPlane) * t;
        splits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    return splits;
}

CascadeFit FitCascade(const Mat4& cameraView, float verticalFov, float aspect, float splitNear, float splitFar,
                      Vec3 lightDirection, std::uint32_t shadowMapSize, float casterDistance) noexcept
{
    // Bounding sphere of the slice. Its centre lies on the view axis at the distance that balances the near and far
    // corners, and its radius depends only on the slice's shape, not on the camera's orientation.
    const float tanHalf = std::tan(verticalFov * 0.5f);
    const float k2 = tanHalf * tanHalf * (1.0f + aspect * aspect); // squared corner offset per unit distance
    const float centreDistance =
        std::min(splitFar, 0.5f * (splitNear + splitFar) * (1.0f + k2)); // clamp keeps wide slices sensible
    const float farCorner = (splitFar - centreDistance) * (splitFar - centreDistance) + k2 * splitFar * splitFar;
    const float nearCorner = (centreDistance - splitNear) * (centreDistance - splitNear) + k2 * splitNear * splitNear;
    float radius = std::sqrt(std::max(farCorner, nearCorner));
    radius = std::ceil(radius * 16.0f) / 16.0f; // quantised so tiny float changes do not change the texel size

    const Mat4 cameraToWorld = Inverse(cameraView);
    const Vec4 centre4 = cameraToWorld * Vec4(0.0f, 0.0f, -centreDistance, 1.0f);
    const Vec3 centre{centre4.x, centre4.y, centre4.z};

    // Light space with a fixed orientation (only the light direction), so snapping is in a stable grid.
    const Vec3 up = std::abs(lightDirection.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Mat4 lightView = LookAt({0.0f, 0.0f, 0.0f}, lightDirection, up);
    const Vec4 lightCentre = lightView * Vec4(centre, 1.0f);

    const float texel = 2.0f * radius / static_cast<float>(shadowMapSize);
    const float snappedX = std::floor(lightCentre.x / texel) * texel;
    const float snappedY = std::floor(lightCentre.y / texel) * texel;
    const float depth = -lightCentre.z; // light view looks down -Z
    const Mat4 projection = OrthographicReverseZ(snappedX - radius, snappedX + radius, snappedY - radius,
                                                 snappedY + radius, depth - radius - casterDistance, depth + radius);
    return {projection * lightView, texel};
}

} // namespace interi::render

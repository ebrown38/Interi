// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

#include <array>
#include <cstdint>

namespace interi::render
{

inline constexpr std::uint32_t kMaxShadowCascades = 4;

/// Cascade boundaries (view distances) between `nearPlane` and `farPlane`: element 0 is nearPlane, element `count`
/// is farPlane. The practical split scheme blends logarithmic (lambda = 1) and uniform (lambda = 0) splits.
[[nodiscard]] std::array<float, kMaxShadowCascades + 1> CascadeSplits(float nearPlane, float farPlane,
                                                                      std::uint32_t count, float lambda) noexcept;

struct CascadeFit
{
    Mat4 viewProjection; ///< World -> shadow clip space (orthographic, reverse-Z).
    float texelWorldSize = 0.0f;
};

/// Fits an orthographic shadow cascade around the camera frustum between view distances splitNear and splitFar.
/// The cascade is a bounding sphere of the slice, so its size does not change as the camera turns, and it is
/// snapped to whole shadow-map texels, so shadows do not shimmer as the camera moves. `lightDirection` is the
/// direction light travels; casters up to `casterDistance` beyond the sphere (towards the light) are included.
[[nodiscard]] CascadeFit FitCascade(const Mat4& cameraView, float verticalFov, float aspect, float splitNear,
                                    float splitFar, Vec3 lightDirection, std::uint32_t shadowMapSize,
                                    float casterDistance = 50.0f) noexcept;

} // namespace interi::render

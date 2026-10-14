// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

namespace interi::render
{

/// Right-handed perspective projection (the camera looks down -Z) with reverse-Z and an infinite far plane:
/// depth is 1 at the near plane and approaches 0 with distance, which spreads float depth precision evenly.
/// `verticalFov` in radians, `aspect` = width / height.
[[nodiscard]] Mat4 PerspectiveReverseZ(float verticalFov, float aspect, float nearPlane) noexcept;

/// Right-handed orthographic projection with reverse-Z (depth 1 at `nearPlane`, 0 at `farPlane`).
[[nodiscard]] Mat4 OrthographicReverseZ(float left, float right, float bottom, float top, float nearPlane,
                                        float farPlane) noexcept;

/// Right-handed view matrix for a camera at `eye` looking at `target`.
[[nodiscard]] Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept;

} // namespace interi::render

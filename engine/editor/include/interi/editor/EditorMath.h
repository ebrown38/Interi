// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

namespace interi::editor
{

/// Euler angles in degrees as the inspector shows them: (pitch about X, yaw about Y, roll about Z), applied roll
/// first, then pitch, then yaw (q = yaw * pitch * roll). Pitch is in [-90, 90].
[[nodiscard]] Vec3 EulerDegrees(const Quat& rotation) noexcept;
[[nodiscard]] Quat FromEulerDegrees(const Vec3& degrees) noexcept;

} // namespace interi::editor

// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

namespace interi
{

/// A third-person orbit camera around a target (the character's eyes). Yaw 0 looks along -Z; positive yaw turns
/// left (counter-clockwise seen from above, right-handed +Y up); positive pitch looks up.
struct FollowCamera
{
    float yaw = 0.0f;      ///< Radians.
    float pitch = -0.25f;  ///< Radians, clamped to [minPitch, maxPitch].
    float distance = 4.0f; ///< m behind the target.
    float shoulder = 0.6f; ///< m to the right of the target (over-the-shoulder aim).
    float minPitch = -1.4f;
    float maxPitch = 1.2f;
    float sensitivity = 0.003f; ///< Radians per pixel of mouse motion.

    /// Mouse (or stick) motion: moving right turns right, moving down looks down.
    void Rotate(float deltaX, float deltaY) noexcept;

    /// Unit view direction.
    [[nodiscard]] Vec3 Forward() const noexcept;
    /// Unit horizontal forward, for walking.
    [[nodiscard]] Vec3 FlatForward() const noexcept;
    /// Unit horizontal right.
    [[nodiscard]] Vec3 Right() const noexcept;
    /// Camera position for `target`.
    [[nodiscard]] Vec3 Eye(const Vec3& target) const noexcept;
    /// World-space walk direction for stick/WASD input (x right, y forward).
    [[nodiscard]] Vec3 MoveDirection(float right, float forward) const noexcept;
};

} // namespace interi

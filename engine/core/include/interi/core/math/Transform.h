// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Vec.h>

namespace interi
{

/// Position, rotation and scale, applied as scale, then rotation, then translation (same as Mat4::FromTRS).
///
/// Composition and Inverse() are exact for uniform scale. With non-uniform scale under rotation the true result
/// contains shear, which a Transform cannot represent; use Mat4 for that case.
struct Transform
{
    Vec3 position;
    Quat rotation;
    Vec3 scale{1.0f};

    [[nodiscard]] static constexpr Transform Identity() noexcept { return {}; }

    [[nodiscard]] constexpr Mat4 ToMat4() const noexcept { return Mat4::FromTRS(position, rotation, scale); }

    [[nodiscard]] Vec3 TransformPoint(const Vec3& p) const noexcept { return position + Rotate(rotation, scale * p); }

    [[nodiscard]] Vec3 TransformVector(const Vec3& v) const noexcept { return Rotate(rotation, scale * v); }

    /// Inverse for uniform scale.
    [[nodiscard]] Transform Inverse() const noexcept
    {
        const Quat inverseRotation = Conjugate(rotation);
        const Vec3 inverseScale{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
        return {.position = inverseScale * Rotate(inverseRotation, -position),
                .rotation = inverseRotation,
                .scale = inverseScale};
    }

    friend constexpr bool operator==(const Transform&, const Transform&) noexcept = default;
};

/// parent * child: the child's local transform expressed in the parent's space (child applied first).
[[nodiscard]] inline Transform operator*(const Transform& parent, const Transform& child) noexcept
{
    return {.position = parent.TransformPoint(child.position),
            .rotation = parent.rotation * child.rotation,
            .scale = parent.scale * child.scale};
}

} // namespace interi

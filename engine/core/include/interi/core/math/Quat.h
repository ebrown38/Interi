// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathKernels.h>
#include <interi/core/math/MathTypes.h>

#include <cmath>

namespace interi
{

/// Composition: (a * b) rotates by b first, then by a.
[[nodiscard]] inline Quat operator*(const Quat& a, const Quat& b) noexcept
{
    return math::active::Multiply(a, b);
}

[[nodiscard]] inline Vec3 Rotate(const Quat& q, const Vec3& v) noexcept
{
    return math::active::Rotate(q, v);
}

[[nodiscard]] constexpr float Dot(const Quat& a, const Quat& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] constexpr Quat Conjugate(const Quat& q) noexcept
{
    return {-q.x, -q.y, -q.z, q.w};
}

[[nodiscard]] inline Quat Normalize(const Quat& q) noexcept
{
    const float length = std::sqrt(Dot(q, q));
    INTERI_ASSERT(length > 0.0f, "normalizing a zero quaternion");
    const float inv = 1.0f / length;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

/// Inverse rotation. Works for non-unit quaternions too; for unit ones it equals Conjugate().
[[nodiscard]] inline Quat Inverse(const Quat& q) noexcept
{
    const float inv = 1.0f / Dot(q, q);
    return {-q.x * inv, -q.y * inv, -q.z * inv, q.w * inv};
}

/// Normalized linear interpolation along the shorter arc. Cheaper than Slerp; angular speed is not constant.
[[nodiscard]] inline Quat Nlerp(const Quat& a, const Quat& b, float t) noexcept
{
    const float sign = Dot(a, b) < 0.0f ? -1.0f : 1.0f;
    return Normalize(Quat{a.x + (sign * b.x - a.x) * t, a.y + (sign * b.y - a.y) * t, a.z + (sign * b.z - a.z) * t,
                          a.w + (sign * b.w - a.w) * t});
}

/// Spherical linear interpolation along the shorter arc, at constant angular speed. Falls back to Nlerp when the
/// rotations are nearly identical (where the slerp weights become 0/0).
[[nodiscard]] inline Quat Slerp(const Quat& a, const Quat& b, float t) noexcept
{
    float cosTheta = Dot(a, b);
    Quat target = b;
    if (cosTheta < 0.0f)
    {
        cosTheta = -cosTheta;
        target = {-b.x, -b.y, -b.z, -b.w};
    }
    if (cosTheta > 0.9995f)
    {
        return Nlerp(a, target, t);
    }
    const float theta = std::acos(cosTheta);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    return {wa * a.x + wb * target.x, wa * a.y + wb * target.y, wa * a.z + wb * target.z, wa * a.w + wb * target.w};
}

} // namespace interi

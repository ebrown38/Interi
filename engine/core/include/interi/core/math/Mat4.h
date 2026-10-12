// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathKernels.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Quat.h>

#include <cmath>
#include <optional>

namespace interi
{

/// a * b applies b first (column vectors).
[[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept
{
    return math::active::Multiply(a, b);
}

[[nodiscard]] inline Vec4 operator*(const Mat4& m, const Vec4& v) noexcept
{
    return math::active::Multiply(m, v);
}

/// Transforms a point (w = 1). The projective divide is not applied.
[[nodiscard]] inline Vec3 TransformPoint(const Mat4& m, const Vec3& p) noexcept
{
    return (m * Vec4(p, 1.0f)).XYZ();
}

/// Transforms a direction (w = 0): translation is ignored.
[[nodiscard]] inline Vec3 TransformVector(const Mat4& m, const Vec3& v) noexcept
{
    return (m * Vec4(v, 0.0f)).XYZ();
}

[[nodiscard]] constexpr Mat4 Transpose(const Mat4& m) noexcept
{
    const Vec4* c = m.columns;
    return {{{c[0].x, c[1].x, c[2].x, c[3].x},
             {c[0].y, c[1].y, c[2].y, c[3].y},
             {c[0].z, c[1].z, c[2].z, c[3].z},
             {c[0].w, c[1].w, c[2].w, c[3].w}}};
}

[[nodiscard]] inline float Determinant(const Mat4& m) noexcept
{
    return math::active::Determinant(m);
}

/// General inverse. The matrix must be invertible; use TryInverse when it might not be.
[[nodiscard]] inline Mat4 Inverse(const Mat4& m) noexcept
{
    return math::active::Inverse(m);
}

/// Inverse, or nothing when |determinant| <= epsilon.
[[nodiscard]] inline std::optional<Mat4> TryInverse(const Mat4& m, float epsilon = 1e-12f) noexcept
{
    if (std::abs(Determinant(m)) <= epsilon)
    {
        return std::nullopt;
    }
    return Inverse(m);
}

[[nodiscard]] constexpr Vec3 GetTranslation(const Mat4& m) noexcept
{
    return m.columns[3].XYZ();
}

} // namespace interi

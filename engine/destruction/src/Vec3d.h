// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cmath>

namespace interi::destruction::detail
{

/// Double-precision vector for geometry that must not lose precision (volumes, cut points, plane tests).
struct Vec3d
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] static constexpr Vec3d From(const Vec3& v) noexcept
    {
        return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
    }
    [[nodiscard]] constexpr Vec3 ToFloat() const noexcept
    {
        return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    }

    friend constexpr Vec3d operator+(const Vec3d& a, const Vec3d& b) noexcept
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    friend constexpr Vec3d operator-(const Vec3d& a, const Vec3d& b) noexcept
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    friend constexpr Vec3d operator*(const Vec3d& a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
};

[[nodiscard]] constexpr double Dot(const Vec3d& a, const Vec3d& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3d Cross(const Vec3d& a, const Vec3d& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline double Length(const Vec3d& v) noexcept
{
    return std::sqrt(Dot(v, v));
}

} // namespace interi::destruction::detail

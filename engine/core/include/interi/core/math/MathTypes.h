// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cmath>

// Quat and Mat4 layouts plus constructors that need no other math. Operators live in Quat.h and Mat4.h, which
// dispatch to the scalar or SIMD kernels in MathKernels.h. Include those headers, not this one.
namespace interi
{

struct Mat4;

/// Unit quaternion representing a rotation. (x, y, z) is the vector part, w the scalar part.
struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] static constexpr Quat Identity() noexcept { return {}; }

    /// Rotation of `radians` about a unit-length `axis` (right-hand rule).
    [[nodiscard]] static Quat FromAxisAngle(const Vec3& axis, float radians) noexcept
    {
        const float half = radians * 0.5f;
        const float s = std::sin(half);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
    }

    /// Extracts the rotation from the upper 3x3 of a pure rotation matrix (Shepperd's method).
    [[nodiscard]] static Quat FromRotationMatrix(const Mat4& m) noexcept;

    friend constexpr bool operator==(const Quat&, const Quat&) noexcept = default;
};

/// 4x4 matrix stored as four columns (column-major). Element (row, column) is columns[column][row].
struct Mat4
{
    Vec4 columns[4];

    [[nodiscard]] static constexpr Mat4 Identity() noexcept
    {
        return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    }
    [[nodiscard]] static constexpr Mat4 Translation(const Vec3& t) noexcept
    {
        return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {t.x, t.y, t.z, 1}}};
    }
    [[nodiscard]] static constexpr Mat4 Scale(const Vec3& s) noexcept
    {
        return {{{s.x, 0, 0, 0}, {0, s.y, 0, 0}, {0, 0, s.z, 0}, {0, 0, 0, 1}}};
    }
    /// Rotation matrix for a unit quaternion.
    [[nodiscard]] static constexpr Mat4 Rotation(const Quat& q) noexcept
    {
        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;
        return {{{1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0},
                 {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0},
                 {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0},
                 {0, 0, 0, 1}}};
    }
    /// Translation * Rotation * Scale: scales first, then rotates, then translates.
    [[nodiscard]] static constexpr Mat4 FromTRS(const Vec3& translation, const Quat& rotation,
                                                const Vec3& scale) noexcept
    {
        Mat4 m = Rotation(rotation);
        m.columns[0] = m.columns[0] * scale.x;
        m.columns[1] = m.columns[1] * scale.y;
        m.columns[2] = m.columns[2] * scale.z;
        m.columns[3] = Vec4(translation, 1.0f);
        return m;
    }

    /// Element at (row, column).
    [[nodiscard]] constexpr float operator()(int row, int column) const noexcept { return columns[column][row]; }
    [[nodiscard]] constexpr float& operator()(int row, int column) noexcept
    {
        Vec4& c = columns[column];
        return row == 0 ? c.x : (row == 1 ? c.y : (row == 2 ? c.z : c.w));
    }

    friend constexpr bool operator==(const Mat4&, const Mat4&) noexcept = default;
};

} // namespace interi

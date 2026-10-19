// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>

#include <algorithm>
#include <cmath>
#include <numbers>

/// Engine math conventions (pinned by tests in tests/core/MathTests.cpp):
///   - Right-handed coordinates, +Y up (same as glTF). Cross(X, Y) == Z.
///   - Column vectors: a point is transformed as p' = M * p, so A * B applies B first.
///   - Mat4 is stored column-major; the translation is column 3.
///   - Quaternions are (x, y, z, w) with w the scalar part; positive angles rotate counter-clockwise when looking
///     down the axis toward the origin.
///   - Angles are radians.
namespace interi
{

inline constexpr float kPi = std::numbers::pi_v<float>;

[[nodiscard]] constexpr float DegreesToRadians(float degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}

/// 3D vector (12 bytes, no padding): positions, directions, extents.
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float xValue, float yValue, float zValue) noexcept
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {
    }
    constexpr explicit Vec3(float scalar) noexcept
        : x(scalar)
        , y(scalar)
        , z(scalar)
    {
    }

    [[nodiscard]] static constexpr Vec3 Zero() noexcept { return {}; }
    [[nodiscard]] static constexpr Vec3 One() noexcept { return Vec3(1.0f); }
    [[nodiscard]] static constexpr Vec3 UnitX() noexcept { return {1, 0, 0}; }
    [[nodiscard]] static constexpr Vec3 UnitY() noexcept { return {0, 1, 0}; }
    [[nodiscard]] static constexpr Vec3 UnitZ() noexcept { return {0, 0, 1}; }

    [[nodiscard]] constexpr float operator[](int index) const noexcept { return index == 0 ? x : (index == 1 ? y : z); }

    constexpr Vec3& operator+=(const Vec3& o) noexcept
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o) noexcept
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(float s) noexcept
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr Vec3& operator/=(float s) noexcept
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, const Vec3& b) noexcept
{
    return a += b;
}
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, const Vec3& b) noexcept
{
    return a -= b;
}
[[nodiscard]] constexpr Vec3 operator-(const Vec3& v) noexcept
{
    return {-v.x, -v.y, -v.z};
}
[[nodiscard]] constexpr Vec3 operator*(Vec3 v, float s) noexcept
{
    return v *= s;
}
[[nodiscard]] constexpr Vec3 operator*(float s, Vec3 v) noexcept
{
    return v *= s;
}
/// Component-wise product.
[[nodiscard]] constexpr Vec3 operator*(const Vec3& a, const Vec3& b) noexcept
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
[[nodiscard]] constexpr Vec3 operator/(Vec3 v, float s) noexcept
{
    return v /= s;
}

[[nodiscard]] constexpr float Dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr float LengthSquared(const Vec3& v) noexcept
{
    return Dot(v, v);
}
[[nodiscard]] inline float Length(const Vec3& v) noexcept
{
    return std::sqrt(LengthSquared(v));
}
[[nodiscard]] inline float Distance(const Vec3& a, const Vec3& b) noexcept
{
    return Length(a - b);
}
/// Unit vector in v's direction. v must not be zero (asserted); see NormalizeOrZero.
[[nodiscard]] inline Vec3 Normalize(const Vec3& v) noexcept
{
    const float length = Length(v);
    INTERI_ASSERT(length > 0.0f, "normalizing a zero vector");
    return v / length;
}
/// Unit vector in v's direction, or zero if v is (nearly) zero.
[[nodiscard]] inline Vec3 NormalizeOrZero(const Vec3& v, float epsilon = 1e-20f) noexcept
{
    const float lengthSquared = LengthSquared(v);
    return lengthSquared > epsilon ? v / std::sqrt(lengthSquared) : Vec3{};
}
[[nodiscard]] constexpr Vec3 Min(const Vec3& a, const Vec3& b) noexcept
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
[[nodiscard]] constexpr Vec3 Max(const Vec3& a, const Vec3& b) noexcept
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
[[nodiscard]] inline Vec3 Abs(const Vec3& v) noexcept
{
    return {std::abs(v.x), std::abs(v.y), std::abs(v.z)};
}
[[nodiscard]] constexpr Vec3 Lerp(const Vec3& a, const Vec3& b, float t) noexcept
{
    return a + (b - a) * t;
}
[[nodiscard]] constexpr float MaxComponent(const Vec3& v) noexcept
{
    return std::max({v.x, v.y, v.z});
}
[[nodiscard]] constexpr float MinComponent(const Vec3& v) noexcept
{
    return std::min({v.x, v.y, v.z});
}

/// Binary serialization (see Archive.h): x, y, z.
template <typename Archive>
void Serialize(Archive& archive, Vec3& v)
{
    archive(v.x, v.y, v.z);
}

/// 4D vector: homogeneous coordinates and matrix columns.
struct Vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue) noexcept
        : x(xValue)
        , y(yValue)
        , z(zValue)
        , w(wValue)
    {
    }
    constexpr Vec4(const Vec3& v, float wValue) noexcept
        : x(v.x)
        , y(v.y)
        , z(v.z)
        , w(wValue)
    {
    }

    [[nodiscard]] constexpr Vec3 XYZ() const noexcept { return {x, y, z}; }
    [[nodiscard]] constexpr float operator[](int index) const noexcept
    {
        return index == 0 ? x : (index == 1 ? y : (index == 2 ? z : w));
    }

    friend constexpr bool operator==(const Vec4&, const Vec4&) noexcept = default;
};

[[nodiscard]] constexpr Vec4 operator+(const Vec4& a, const Vec4& b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
[[nodiscard]] constexpr Vec4 operator-(const Vec4& a, const Vec4& b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
[[nodiscard]] constexpr Vec4 operator*(const Vec4& v, float s) noexcept
{
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}
[[nodiscard]] constexpr float Dot(const Vec4& a, const Vec4& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// Binary serialization (see Archive.h): x, y, z, w.
template <typename Archive>
void Serialize(Archive& archive, Vec4& v)
{
    archive(v.x, v.y, v.z, v.w);
}

} // namespace interi

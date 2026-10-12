// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Mat4.h>
#include <interi/core/math/Vec.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace interi
{

/// Axis-aligned bounding box. Empty() is inverted (min > max) so that expanding it by any point gives that point.
struct AABB
{
    Vec3 min;
    Vec3 max;

    [[nodiscard]] static constexpr AABB Empty() noexcept
    {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return {.min = Vec3(inf), .max = Vec3(-inf)};
    }

    [[nodiscard]] static constexpr AABB FromPoints(std::span<const Vec3> points) noexcept
    {
        AABB box = Empty();
        for (const Vec3& p : points)
        {
            box.Expand(p);
        }
        return box;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    constexpr void Expand(const Vec3& p) noexcept
    {
        min = interi::Min(min, p);
        max = interi::Max(max, p);
    }
    constexpr void Merge(const AABB& other) noexcept
    {
        min = interi::Min(min, other.min);
        max = interi::Max(max, other.max);
    }

    [[nodiscard]] constexpr Vec3 Center() const noexcept { return (min + max) * 0.5f; }
    /// Half the size along each axis.
    [[nodiscard]] constexpr Vec3 Extents() const noexcept { return (max - min) * 0.5f; }
    [[nodiscard]] constexpr float SurfaceArea() const noexcept
    {
        const Vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    /// Inclusive containment, optionally grown by `epsilon` on every side.
    [[nodiscard]] constexpr bool Contains(const Vec3& p, float epsilon = 0.0f) const noexcept
    {
        return p.x >= min.x - epsilon && p.x <= max.x + epsilon && p.y >= min.y - epsilon && p.y <= max.y + epsilon &&
               p.z >= min.z - epsilon && p.z <= max.z + epsilon;
    }
    /// True if the boxes overlap or touch.
    [[nodiscard]] constexpr bool Intersects(const AABB& o) const noexcept
    {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z &&
               max.z >= o.min.z;
    }

    /// Tight box around this box transformed by m (Arvo's method: per-axis min/max of the matrix terms).
    [[nodiscard]] AABB Transformed(const Mat4& m) const noexcept
    {
        const Vec3 translation = GetTranslation(m);
        AABB result{.min = translation, .max = translation};
        for (int column = 0; column < 3; ++column)
        {
            const Vec3 axis = m.columns[column].XYZ();
            const Vec3 a = axis * min[column];
            const Vec3 b = axis * max[column];
            result.min += interi::Min(a, b);
            result.max += interi::Max(a, b);
        }
        return result;
    }
};

/// Plane of points p with Dot(normal, p) + d == 0. The normal is unit length; SignedDistance is positive on
/// the side the normal points to ("front").
struct Plane
{
    Vec3 normal{0, 1, 0};
    float d = 0.0f;

    [[nodiscard]] static Plane FromPointNormal(const Vec3& point, const Vec3& normal) noexcept
    {
        const Vec3 n = Normalize(normal);
        return {.normal = n, .d = -Dot(n, point)};
    }
    /// Plane through a, b, c; the normal faces the side from which a, b, c appear counter-clockwise.
    [[nodiscard]] static Plane FromPoints(const Vec3& a, const Vec3& b, const Vec3& c) noexcept
    {
        return FromPointNormal(a, Cross(b - a, c - a));
    }

    [[nodiscard]] constexpr float SignedDistance(const Vec3& p) const noexcept { return Dot(normal, p) + d; }
};

/// Half-line origin + t * direction, t >= 0. The direction need not be normalized; t is then in its units.
struct Ray
{
    Vec3 origin;
    Vec3 direction{0, 0, 1};

    [[nodiscard]] constexpr Vec3 At(float t) const noexcept { return origin + direction * t; }
};

/// Distance t >= 0 to the plane, or nothing if the ray is parallel to it or points away.
[[nodiscard]] inline std::optional<float> Intersect(const Ray& ray, const Plane& plane) noexcept
{
    const float denominator = Dot(plane.normal, ray.direction);
    if (std::abs(denominator) < 1e-12f)
    {
        return std::nullopt;
    }
    const float t = -plane.SignedDistance(ray.origin) / denominator;
    return t >= 0.0f ? std::optional<float>(t) : std::nullopt;
}

/// Entry distance into the box (slab method); 0 when the origin is inside.
[[nodiscard]] inline std::optional<float> Intersect(const Ray& ray, const AABB& box) noexcept
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        if (std::abs(direction) < 1e-20f)
        {
            if (origin < box.min[axis] || origin > box.max[axis])
            {
                return std::nullopt;
            }
            continue;
        }
        const float inverse = 1.0f / direction;
        float t0 = (box.min[axis] - origin) * inverse;
        float t1 = (box.max[axis] - origin) * inverse;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax)
        {
            return std::nullopt;
        }
    }
    return tMin;
}

/// Ray/triangle hit: distance and barycentric coordinates (point = (1-u-v)*a + u*b + v*c).
struct TriangleHit
{
    float t;
    float u;
    float v;
};

/// Möller-Trumbore intersection; both faces are hit.
[[nodiscard]] inline std::optional<TriangleHit> Intersect(const Ray& ray, const Vec3& a, const Vec3& b,
                                                          const Vec3& c) noexcept
{
    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 p = Cross(ray.direction, edge2);
    const float determinant = Dot(edge1, p);
    if (std::abs(determinant) < 1e-12f)
    {
        return std::nullopt;
    }
    const float inverse = 1.0f / determinant;
    const Vec3 s = ray.origin - a;
    const float u = Dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f)
    {
        return std::nullopt;
    }
    const Vec3 q = Cross(s, edge1);
    const float v = Dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
    {
        return std::nullopt;
    }
    const float t = Dot(edge2, q) * inverse;
    return t >= 0.0f ? std::optional<TriangleHit>(TriangleHit{.t = t, .u = u, .v = v}) : std::nullopt;
}

} // namespace interi

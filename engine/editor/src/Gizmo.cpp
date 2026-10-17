// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/Gizmo.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace interi::editor
{

namespace
{

constexpr float kPickRadius = 0.08f; ///< Of the gizmo size.
constexpr int kRingSegments = 64;    ///< Rings are picked as polylines.
constexpr float kParallel = 1e-4f;

/// Closest distance between a ray and the segment [a, b].
[[nodiscard]] float RaySegmentDistance(const Ray& ray, const Vec3& a, const Vec3& b) noexcept
{
    const Vec3 u = ray.direction;
    const Vec3 v = b - a;
    const Vec3 w = ray.origin - a;
    const float uv = Dot(u, v);
    const float vv = Dot(v, v);
    const float uw = Dot(u, w);
    const float vw = Dot(v, w);
    const float denominator = vv - uv * uv; // |u| = 1
    float s = 0.0f;                         // along the segment, 0..1
    if (denominator > kParallel * vv)
    {
        s = std::clamp((vw - uv * uw) / denominator, 0.0f, 1.0f);
    }
    // The ray point closest to the segment point, clamped to the ray's start.
    const Vec3 onSegment = a + v * s;
    const float t = std::max(Dot(onSegment - ray.origin, u), 0.0f);
    return Length(ray.origin + u * t - onSegment);
}

/// Two unit vectors perpendicular to `axis` and each other.
void Basis(const Vec3& axis, Vec3& tangent, Vec3& bitangent) noexcept
{
    const Vec3 helper = std::abs(axis.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    tangent = Normalize(Cross(helper, axis));
    bitangent = Cross(axis, tangent);
}

} // namespace

float Snap(float value, float grid) noexcept
{
    if (!(grid > 0.0f))
    {
        return value;
    }
    return std::round(value / grid) * grid;
}

Vec3 Snap(const Vec3& value, float grid) noexcept
{
    return {Snap(value.x, grid), Snap(value.y, grid), Snap(value.z, grid)};
}

Vec3 AxisVector(GizmoAxis axis) noexcept
{
    switch (axis)
    {
    case GizmoAxis::X:
        return {1.0f, 0.0f, 0.0f};
    case GizmoAxis::Y:
        return {0.0f, 1.0f, 0.0f};
    case GizmoAxis::Z:
        return {0.0f, 0.0f, 1.0f};
    case GizmoAxis::None:
        break;
    }
    return {};
}

float Gizmo::SizeFor(const Vec3& origin, const Vec3& eye) noexcept
{
    return std::max(Length(eye - origin) * 0.15f, 0.05f);
}

GizmoAxis GizmoRayPick(const Gizmo& gizmo, const Ray& ray) noexcept
{
    GizmoAxis best = GizmoAxis::None;
    float bestDistance = kPickRadius * gizmo.size;
    for (const GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z})
    {
        const Vec3 direction = AxisVector(axis);
        float distance = std::numeric_limits<float>::max();
        if (gizmo.mode == GizmoMode::Rotate)
        {
            Vec3 tangent;
            Vec3 bitangent;
            Basis(direction, tangent, bitangent);
            Vec3 previous = gizmo.origin + tangent * gizmo.size;
            for (int i = 1; i <= kRingSegments; ++i)
            {
                const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kRingSegments;
                const Vec3 point =
                    gizmo.origin + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * gizmo.size;
                distance = std::min(distance, RaySegmentDistance(ray, previous, point));
                previous = point;
            }
        }
        else
        {
            distance = RaySegmentDistance(ray, gizmo.origin, gizmo.origin + direction * gizmo.size);
        }
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = axis;
        }
    }
    return best;
}

std::optional<GizmoDrag> GizmoDrag::Begin(const Gizmo& gizmo, GizmoAxis axis, const Ray& ray) noexcept
{
    if (axis == GizmoAxis::None)
    {
        return std::nullopt;
    }
    GizmoDrag drag;
    drag.m_mode = gizmo.mode;
    drag.m_axis = axis;
    drag.m_origin = gizmo.origin;
    drag.m_direction = AxisVector(axis);
    if (gizmo.mode == GizmoMode::Rotate)
    {
        const auto vector = drag.PlaneVector(ray);
        if (!vector)
        {
            return std::nullopt;
        }
        drag.m_startVector = *vector;
    }
    else
    {
        const auto parameter = drag.AxisParameter(ray);
        if (!parameter)
        {
            return std::nullopt;
        }
        drag.m_startParameter = *parameter;
    }
    return drag;
}

std::optional<float> GizmoDrag::AxisParameter(const Ray& ray) const noexcept
{
    // Closest point on the axis line to the ray: origin + direction * s.
    const Vec3 w = ray.origin - m_origin;
    const float b = Dot(ray.direction, m_direction);
    const float denominator = 1.0f - b * b;
    if (denominator < kParallel)
    {
        return std::nullopt;
    }
    return (Dot(m_direction, w) - b * Dot(ray.direction, w)) / denominator;
}

std::optional<Vec3> GizmoDrag::PlaneVector(const Ray& ray) const noexcept
{
    const float facing = Dot(ray.direction, m_direction);
    if (std::abs(facing) < kParallel)
    {
        return std::nullopt;
    }
    const float t = Dot(m_origin - ray.origin, m_direction) / facing;
    const Vec3 offset = ray.origin + ray.direction * t - m_origin;
    if (Length(offset) < 1e-6f)
    {
        return std::nullopt;
    }
    return offset;
}

float GizmoDrag::Distance(const Ray& ray) const noexcept
{
    const auto parameter = AxisParameter(ray);
    return parameter ? *parameter - m_startParameter : 0.0f;
}

float GizmoDrag::Angle(const Ray& ray) const noexcept
{
    const auto vector = PlaneVector(ray);
    if (!vector)
    {
        return 0.0f;
    }
    return std::atan2(Dot(Cross(m_startVector, *vector), m_direction), Dot(m_startVector, *vector));
}

float GizmoDrag::Factor(const Ray& ray) const noexcept
{
    const auto parameter = AxisParameter(ray);
    if (!parameter || std::abs(m_startParameter) < 1e-6f)
    {
        return 1.0f;
    }
    return *parameter / m_startParameter;
}

std::optional<float> RayBox(const Ray& ray, const Vec3& centre, const Quat& rotation, const Vec3& halfExtents) noexcept
{
    // Slab test in the box's frame.
    const Quat inverse = Inverse(rotation);
    const Vec3 origin = Rotate(inverse, ray.origin - centre);
    const Vec3 direction = Rotate(inverse, ray.direction);
    float enter = 0.0f;
    float leave = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float o = origin[axis];
        const float d = direction[axis];
        const float extent = halfExtents[axis];
        if (std::abs(d) < 1e-8f)
        {
            if (o < -extent || o > extent)
            {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (-extent - o) / d;
        float t1 = (extent - o) / d;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave)
        {
            return std::nullopt;
        }
    }
    return enter;
}

std::optional<float> RayPlaneY(const Ray& ray, float height) noexcept
{
    if (std::abs(ray.direction.y) < 1e-6f)
    {
        return std::nullopt;
    }
    const float t = (height - ray.origin.y) / ray.direction.y;
    if (t < 0.0f)
    {
        return std::nullopt;
    }
    return t;
}

} // namespace interi::editor

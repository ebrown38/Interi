// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/editor/EditorCamera.h>

#include <array>
#include <cstdint>
#include <optional>

namespace interi::editor
{

/// `value` rounded to the nearest multiple of `grid` (halfway cases away from zero). A grid <= 0 leaves it alone.
[[nodiscard]] float Snap(float value, float grid) noexcept;
[[nodiscard]] Vec3 Snap(const Vec3& value, float grid) noexcept;

enum class GizmoMode : std::uint8_t
{
    Translate,
    Rotate,
    Scale,
};

enum class GizmoAxis : std::uint8_t
{
    None,
    X,
    Y,
    Z,
};

/// The world axis of a gizmo handle (unit +X, +Y or +Z; zero for None).
[[nodiscard]] Vec3 AxisVector(GizmoAxis axis) noexcept;

/// A transform gizmo in world space: three handles along the world axes from `origin`, `size` metres long
/// (translate and scale: segments; rotate: rings of radius `size` around each axis).
struct Gizmo
{
    GizmoMode mode = GizmoMode::Translate;
    Vec3 origin;
    float size = 1.0f;

    /// A size that keeps the gizmo about the same on screen: a fraction of the distance to the eye.
    [[nodiscard]] static float SizeFor(const Vec3& origin, const Vec3& eye) noexcept;
};

/// The handle the ray passes closest to, within a pick radius of 8% of the gizmo size; None if it misses all of
/// them. Translate and scale handles are segments, rotate handles are rings.
[[nodiscard]] GizmoAxis GizmoRayPick(const Gizmo& gizmo, const Ray& ray) noexcept;

/// One drag of a gizmo handle: the mouse ray at the start and now give the movement along (translate, scale) or
/// around (rotate) the handle's axis.
class GizmoDrag
{
public:
    /// Nothing if the ray can't grab the handle (parallel to the axis, or to a ring's plane).
    [[nodiscard]] static std::optional<GizmoDrag> Begin(const Gizmo& gizmo, GizmoAxis axis, const Ray& ray) noexcept;

    [[nodiscard]] GizmoAxis Axis() const noexcept { return m_axis; }
    [[nodiscard]] GizmoMode Mode() const noexcept { return m_mode; }

    /// Translate: metres moved along the axis since Begin (0 if the ray became parallel).
    [[nodiscard]] float Distance(const Ray& ray) const noexcept;
    /// Rotate: radians turned about the axis since Begin, counter-clockwise looking down the axis, in (-pi, pi].
    [[nodiscard]] float Angle(const Ray& ray) const noexcept;
    /// Scale: the ratio of the handle's current length to its length at Begin (1 if undefined).
    [[nodiscard]] float Factor(const Ray& ray) const noexcept;

private:
    GizmoDrag() = default;
    [[nodiscard]] std::optional<float> AxisParameter(const Ray& ray) const noexcept;
    [[nodiscard]] std::optional<Vec3> PlaneVector(const Ray& ray) const noexcept;

    GizmoMode m_mode = GizmoMode::Translate;
    GizmoAxis m_axis = GizmoAxis::None;
    Vec3 m_origin;
    Vec3 m_direction;
    float m_startParameter = 0.0f;
    Vec3 m_startVector;
};

/// Distance along the ray to where it enters an oriented box, or nothing if it misses (0 if it starts inside).
[[nodiscard]] std::optional<float> RayBox(const Ray& ray, const Vec3& centre, const Quat& rotation,
                                          const Vec3& halfExtents) noexcept;

/// Distance along the ray to the horizontal plane at `height`, or nothing if it points away or along it.
[[nodiscard]] std::optional<float> RayPlaneY(const Ray& ray, float height) noexcept;

} // namespace interi::editor

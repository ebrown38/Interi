// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Geometry.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

#include <optional>

namespace interi::editor
{

/// A point in a viewport, in pixels from its top-left corner.
struct ScreenPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

/// The viewport camera: orbits `target` at `distance`. Uses the renderer's conventions (LookAt, reverse-Z infinite
/// perspective), so picking and overlays line up with what is drawn.
struct OrbitCamera
{
    Vec3 target{0.0f, 1.0f, 0.0f};
    float yaw = 0.6f;       ///< Radians about +Y; 0 puts the eye on +Z of the target.
    float pitch = 0.45f;    ///< Radians above the target's horizon, within +-1.5.
    float distance = 25.0f; ///< Metres, at least 0.5.
    float verticalFov = 1.0471976f;
    float nearPlane = 0.1f;

    [[nodiscard]] Vec3 Eye() const noexcept;
    [[nodiscard]] Mat4 View() const noexcept;
    [[nodiscard]] Mat4 ViewProjection(float width, float height) const noexcept;

    /// Mouse-driven moves: orbit by pixels dragged, pan in the view plane, zoom by wheel steps.
    void Orbit(float dx, float dy) noexcept;
    void Pan(float dx, float dy, float viewportHeight) noexcept;
    void Zoom(float steps) noexcept;

    /// The ray from the eye through a viewport pixel (unit direction).
    [[nodiscard]] Ray ScreenRay(ScreenPoint point, float width, float height) const noexcept;
    /// Where a world point shows in the viewport; nothing if it is behind the eye.
    [[nodiscard]] std::optional<ScreenPoint> Project(const Vec3& world, float width, float height) const noexcept;
};

} // namespace interi::editor

// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/editor/EditorCamera.h>
#include <interi/render/Camera.h>

#include <algorithm>
#include <cmath>

namespace interi::editor
{

namespace
{

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};

} // namespace

Vec3 OrbitCamera::Eye() const noexcept
{
    const Vec3 offset{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    return target + offset * distance;
}

Mat4 OrbitCamera::View() const noexcept
{
    return render::LookAt(Eye(), target, kUp);
}

Mat4 OrbitCamera::ViewProjection(float width, float height) const noexcept
{
    return render::PerspectiveReverseZ(verticalFov, width / std::max(height, 1.0f), nearPlane) * View();
}

void OrbitCamera::Orbit(float dx, float dy) noexcept
{
    yaw -= dx * 0.008f;
    pitch = std::clamp(pitch + dy * 0.008f, -1.5f, 1.5f);
}

void OrbitCamera::Pan(float dx, float dy, float viewportHeight) noexcept
{
    const Vec3 forward = Normalize(target - Eye());
    const Vec3 right = Normalize(Cross(forward, kUp));
    const Vec3 up = Cross(right, forward);
    // One pixel moves the target by the size of a pixel at the target's distance.
    const float metresPerPixel = 2.0f * distance * std::tan(verticalFov * 0.5f) / std::max(viewportHeight, 1.0f);
    target = target + (right * -dx + up * dy) * metresPerPixel;
}

void OrbitCamera::Zoom(float steps) noexcept
{
    distance = std::clamp(distance * std::pow(0.88f, steps), 0.5f, 2000.0f);
}

Ray OrbitCamera::ScreenRay(ScreenPoint point, float width, float height) const noexcept
{
    const Vec3 eye = Eye();
    const Vec3 forward = Normalize(target - eye);
    const Vec3 right = Normalize(Cross(forward, kUp));
    const Vec3 up = Cross(right, forward);
    const float tanHalf = std::tan(verticalFov * 0.5f);
    const float aspect = width / std::max(height, 1.0f);
    const float ndcX = 2.0f * point.x / std::max(width, 1.0f) - 1.0f;
    const float ndcY = 1.0f - 2.0f * point.y / std::max(height, 1.0f);
    return {.origin = eye, .direction = Normalize(forward + right * (ndcX * tanHalf * aspect) + up * (ndcY * tanHalf))};
}

std::optional<ScreenPoint> OrbitCamera::Project(const Vec3& world, float width, float height) const noexcept
{
    const Vec4 clip = ViewProjection(width, height) * Vec4(world, 1.0f);
    if (clip.w <= 1e-6f)
    {
        return std::nullopt;
    }
    return ScreenPoint{.x = (clip.x / clip.w + 1.0f) * 0.5f * width, .y = (1.0f - clip.y / clip.w) * 0.5f * height};
}

} // namespace interi::editor

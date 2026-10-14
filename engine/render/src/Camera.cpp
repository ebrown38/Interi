// SPDX-License-Identifier: MIT
#include <interi/render/Camera.h>

#include <cmath>

namespace interi::render
{

Mat4 PerspectiveReverseZ(float verticalFov, float aspect, float nearPlane) noexcept
{
    // clip.z = near, clip.w = -z_view, so depth = near / -z_view: 1 at the near plane, -> 0 at infinity.
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 m{};
    m.columns[0] = {f / aspect, 0, 0, 0};
    m.columns[1] = {0, f, 0, 0};
    m.columns[2] = {0, 0, 0, -1};
    m.columns[3] = {0, 0, nearPlane, 0};
    return m;
}

Mat4 OrthographicReverseZ(float left, float right, float bottom, float top, float nearPlane, float farPlane) noexcept
{
    // depth = (z_view + far) / (far - near): 1 at z_view = -near, 0 at z_view = -far.
    const float range = farPlane - nearPlane;
    Mat4 m{};
    m.columns[0] = {2.0f / (right - left), 0, 0, 0};
    m.columns[1] = {0, 2.0f / (top - bottom), 0, 0};
    m.columns[2] = {0, 0, 1.0f / range, 0};
    m.columns[3] = {-(right + left) / (right - left), -(top + bottom) / (top - bottom), farPlane / range, 1};
    return m;
}

Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept
{
    const Vec3 forward = Normalize(target - eye);
    const Vec3 right = Normalize(Cross(forward, up));
    const Vec3 trueUp = Cross(right, forward);
    Mat4 m{};
    m.columns[0] = {right.x, trueUp.x, -forward.x, 0};
    m.columns[1] = {right.y, trueUp.y, -forward.y, 0};
    m.columns[2] = {right.z, trueUp.z, -forward.z, 0};
    m.columns[3] = {-Dot(right, eye), -Dot(trueUp, eye), Dot(forward, eye), 1};
    return m;
}

} // namespace interi::render

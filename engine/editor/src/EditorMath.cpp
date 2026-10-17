// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/EditorMath.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::editor
{

namespace
{

constexpr float kToDegrees = 180.0f / std::numbers::pi_v<float>;
constexpr float kToRadians = std::numbers::pi_v<float> / 180.0f;

} // namespace

Vec3 EulerDegrees(const Quat& rotation) noexcept
{
    const Quat q = Normalize(rotation);
    // Elements of the rotation matrix R = Ry(yaw) Rx(pitch) Rz(roll) (row, column).
    const float m02 = 2.0f * (q.x * q.z + q.w * q.y);
    const float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    const float m12 = 2.0f * (q.y * q.z - q.w * q.x);
    const float m10 = 2.0f * (q.x * q.y + q.w * q.z);
    const float m11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    const float pitch = std::asin(std::clamp(-m12, -1.0f, 1.0f));
    return Vec3(pitch, std::atan2(m02, m22), std::atan2(m10, m11)) * kToDegrees;
}

Quat FromEulerDegrees(const Vec3& degrees) noexcept
{
    const Vec3 radians = degrees * kToRadians;
    return Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, radians.y) * Quat::FromAxisAngle({1.0f, 0.0f, 0.0f}, radians.x) *
           Quat::FromAxisAngle({0.0f, 0.0f, 1.0f}, radians.z);
}

} // namespace interi::editor

// SPDX-License-Identifier: MIT
#include <interi/runtime/FollowCamera.h>

#include <algorithm>
#include <cmath>

namespace interi
{

void FollowCamera::Rotate(float deltaX, float deltaY) noexcept
{
    yaw -= deltaX * sensitivity;
    pitch = std::clamp(pitch - deltaY * sensitivity, minPitch, maxPitch);
}

Vec3 FollowCamera::Forward() const noexcept
{
    const float cosPitch = std::cos(pitch);
    return {-std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch};
}

Vec3 FollowCamera::FlatForward() const noexcept
{
    return {-std::sin(yaw), 0.0f, -std::cos(yaw)};
}

Vec3 FollowCamera::Right() const noexcept
{
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}

Vec3 FollowCamera::Eye(const Vec3& target) const noexcept
{
    return target - Forward() * distance + Right() * shoulder;
}

Vec3 FollowCamera::MoveDirection(float right, float forward) const noexcept
{
    return Right() * right + FlatForward() * forward;
}

} // namespace interi

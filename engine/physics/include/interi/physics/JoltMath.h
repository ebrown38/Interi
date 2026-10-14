// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>
// clang-format on

// Conversions between Interi and Jolt math types. Both use column vectors, column-major matrices with translation in
// column 3, and (x, y, z, w) quaternions, so every conversion is a plain copy (pinned by JoltMathTests).
#ifdef JPH_DOUBLE_PRECISION
#error "Interi uses single-precision Jolt: JPH::RVec3 must be JPH::Vec3"
#endif

namespace interi::physics
{

[[nodiscard]] inline JPH::Vec3 ToJolt(const Vec3& v) noexcept
{
    return {v.x, v.y, v.z};
}

[[nodiscard]] inline JPH::Vec4 ToJolt(const Vec4& v) noexcept
{
    return {v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline JPH::Quat ToJolt(const Quat& q) noexcept
{
    return {q.x, q.y, q.z, q.w};
}

[[nodiscard]] inline JPH::Mat44 ToJolt(const Mat4& m) noexcept
{
    return {ToJolt(m.columns[0]), ToJolt(m.columns[1]), ToJolt(m.columns[2]), ToJolt(m.columns[3])};
}

[[nodiscard]] inline Vec3 FromJolt(JPH::Vec3Arg v) noexcept
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}

[[nodiscard]] inline Vec4 FromJolt(JPH::Vec4Arg v) noexcept
{
    return {v.GetX(), v.GetY(), v.GetZ(), v.GetW()};
}

[[nodiscard]] inline Quat FromJolt(JPH::QuatArg q) noexcept
{
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

[[nodiscard]] inline Mat4 FromJolt(JPH::Mat44Arg m) noexcept
{
    return {
        {FromJolt(m.GetColumn4(0)), FromJolt(m.GetColumn4(1)), FromJolt(m.GetColumn4(2)), FromJolt(m.GetColumn4(3))}};
}

} // namespace interi::physics

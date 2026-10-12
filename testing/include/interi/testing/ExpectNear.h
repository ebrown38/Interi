// SPDX-License-Identifier: MIT
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>

namespace interi::testing
{

/// Any type with public float-convertible x, y, z members (engine math types, test structs).
template <typename T>
concept Vec3Like = requires(const T& v) {
    { v.x } -> std::convertible_to<float>;
    { v.y } -> std::convertible_to<float>;
    { v.z } -> std::convertible_to<float>;
};

/// Vec3Like plus a w member (Vec4, quaternions).
template <typename T>
concept Vec4Like = Vec3Like<T> && requires(const T& v) {
    { v.w } -> std::convertible_to<float>;
};

namespace detail
{
template <Vec3Like T>
std::array<float, 3> Components(const T& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

template <Vec4Like T>
std::array<float, 4> Components(const T& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), static_cast<float>(v.w)};
}

/// Component-wise |a - sign*b| <= epsilon. NaN never compares near.
template <std::size_t N>
::testing::AssertionResult ComponentsNear(const std::array<float, N>& a, const std::array<float, N>& b, float epsilon,
                                          float signB = 1.0f)
{
    static constexpr const char* kNames[] = {"x", "y", "z", "w"};
    bool ok = true;
    ::testing::AssertionResult failure = ::testing::AssertionFailure();
    failure << "values differ by more than " << epsilon << ":";
    for (std::size_t i = 0; i < N; ++i)
    {
        const float expected = signB * b[i];
        if (!(std::abs(a[i] - expected) <= epsilon))
        {
            ok = false;
            failure << "\n  " << kNames[i] << ": " << a[i] << " vs " << expected << " (diff "
                    << std::abs(a[i] - expected) << ")";
        }
    }
    return ok ? ::testing::AssertionSuccess() : failure;
}
} // namespace detail

/// Component-wise near-equality for 3- or 4-component vectors. Any NaN component fails.
template <Vec3Like A, Vec3Like B>
::testing::AssertionResult VecNear(const A& a, const B& b, float epsilon)
{
    return detail::ComponentsNear(detail::Components(a), detail::Components(b), epsilon);
}

/// Quaternions q and -q encode the same rotation, so either sign of b may match a.
template <Vec4Like A, Vec4Like B>
::testing::AssertionResult QuatNear(const A& a, const B& b, float epsilon)
{
    const auto ca = detail::Components(a);
    const auto cb = detail::Components(b);
    if (detail::ComponentsNear(ca, cb, epsilon))
    {
        return ::testing::AssertionSuccess();
    }
    return detail::ComponentsNear(ca, cb, epsilon, -1.0f) << "\n  (also compared against the negated quaternion)";
}

} // namespace interi::testing

#define INTERI_EXPECT_VEC_NEAR(a, b, epsilon) EXPECT_TRUE(::interi::testing::VecNear((a), (b), (epsilon)))
#define INTERI_ASSERT_VEC_NEAR(a, b, epsilon) ASSERT_TRUE(::interi::testing::VecNear((a), (b), (epsilon)))
#define INTERI_EXPECT_QUAT_NEAR(a, b, epsilon) EXPECT_TRUE(::interi::testing::QuatNear((a), (b), (epsilon)))
#define INTERI_ASSERT_QUAT_NEAR(a, b, epsilon) ASSERT_TRUE(::interi::testing::QuatNear((a), (b), (epsilon)))

// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>

#include <cmath>

namespace interi
{

Quat Quat::FromRotationMatrix(const Mat4& m) noexcept
{
    // Shepperd: pick the largest of w, x, y, z to divide by, which keeps the result well conditioned.
    const float m00 = m(0, 0);
    const float m11 = m(1, 1);
    const float m22 = m(2, 2);
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f; // 4w
        q = {(m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s, 0.25f * s};
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f; // 4x
        q = {0.25f * s, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s, (m(2, 1) - m(1, 2)) / s};
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f; // 4y
        q = {(m(0, 1) + m(1, 0)) / s, 0.25f * s, (m(1, 2) + m(2, 1)) / s, (m(0, 2) - m(2, 0)) / s};
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f; // 4z
        q = {(m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, 0.25f * s, (m(1, 0) - m(0, 1)) / s};
    }
    return Normalize(q);
}

} // namespace interi

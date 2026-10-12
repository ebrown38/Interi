// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/MathTypes.h>

#include <cstddef>
#include <span>

#if defined(INTERI_SIMD_SSE4)
#include <immintrin.h>
#define INTERI_HAS_SIMD_KERNELS 1
#else
#define INTERI_HAS_SIMD_KERNELS 0
#endif

/// The math operations that benefit from SIMD, each implemented twice:
///   interi::math::scalar - portable reference, compiled in every build.
///   interi::math::simd   - SSE4.1 (128-bit), compiled when INTERI_SIMD_SSE4 is defined by CMake.
/// interi::math::active names whichever the build uses; Quat.h / Mat4.h operators call it.
///
/// Where the SIMD and scalar code perform the same float operations in the same order (matrix products and
/// point transforms), results are bit-identical, which tests assert. No approximate instructions (rcp/rsqrt) are
/// used: results must not depend on the CPU vendor.
namespace interi::math
{

namespace scalar
{

/// m * v, summed as ((c0*x + c1*y) + c2*z) + c3*w.
[[nodiscard]] inline Vec4 Multiply(const Mat4& m, const Vec4& v) noexcept
{
    const Vec4& c0 = m.columns[0];
    const Vec4& c1 = m.columns[1];
    const Vec4& c2 = m.columns[2];
    const Vec4& c3 = m.columns[3];
    return {
        ((c0.x * v.x + c1.x * v.y) + c2.x * v.z) + c3.x * v.w, ((c0.y * v.x + c1.y * v.y) + c2.y * v.z) + c3.y * v.w,
        ((c0.z * v.x + c1.z * v.y) + c2.z * v.z) + c3.z * v.w, ((c0.w * v.x + c1.w * v.y) + c2.w * v.z) + c3.w * v.w};
}

[[nodiscard]] inline Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept
{
    return {
        {Multiply(a, b.columns[0]), Multiply(a, b.columns[1]), Multiply(a, b.columns[2]), Multiply(a, b.columns[3])}};
}

/// Determinant via the cross-product formulation used by Inverse().
[[nodiscard]] inline float Determinant(const Mat4& m) noexcept
{
    const Vec3 a = m.columns[0].XYZ();
    const Vec3 b = m.columns[1].XYZ();
    const Vec3 c = m.columns[2].XYZ();
    const Vec3 d = m.columns[3].XYZ();
    const float x = m.columns[0].w;
    const float y = m.columns[1].w;
    const float z = m.columns[2].w;
    const float w = m.columns[3].w;
    const Vec3 s = Cross(a, b);
    const Vec3 t = Cross(c, d);
    const Vec3 u = a * y - b * x;
    const Vec3 v = c * w - d * z;
    return Dot(s, v) + Dot(t, u);
}

/// General 4x4 inverse (E. Lengyel, Foundations of Game Engine Development vol. 1, sec. 1.7.5). The result is
/// non-finite for singular matrices.
[[nodiscard]] inline Mat4 Inverse(const Mat4& m) noexcept
{
    const Vec3 a = m.columns[0].XYZ();
    const Vec3 b = m.columns[1].XYZ();
    const Vec3 c = m.columns[2].XYZ();
    const Vec3 d = m.columns[3].XYZ();
    const float x = m.columns[0].w;
    const float y = m.columns[1].w;
    const float z = m.columns[2].w;
    const float w = m.columns[3].w;

    Vec3 s = Cross(a, b);
    Vec3 t = Cross(c, d);
    Vec3 u = a * y - b * x;
    Vec3 v = c * w - d * z;
    const float invDet = 1.0f / (Dot(s, v) + Dot(t, u));
    s *= invDet;
    t *= invDet;
    u *= invDet;
    v *= invDet;

    const Vec3 r0 = Cross(b, v) + t * y;
    const Vec3 r1 = Cross(v, a) - t * x;
    const Vec3 r2 = Cross(d, u) + s * w;
    const Vec3 r3 = Cross(u, c) - s * z;
    const Vec4 row0{r0, -Dot(b, t)};
    const Vec4 row1{r1, Dot(a, t)};
    const Vec4 row2{r2, -Dot(d, s)};
    const Vec4 row3{r3, Dot(c, s)};
    return {{{row0.x, row1.x, row2.x, row3.x},
             {row0.y, row1.y, row2.y, row3.y},
             {row0.z, row1.z, row2.z, row3.z},
             {row0.w, row1.w, row2.w, row3.w}}};
}

/// Hamilton product a * b (apply b, then a).
[[nodiscard]] inline Quat Multiply(const Quat& a, const Quat& b) noexcept
{
    return {((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y, ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
            ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w, ((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z};
}

/// Rotates v by unit quaternion q: v + w*t + q.xyz x t, with t = 2 * (q.xyz x v).
[[nodiscard]] inline Vec3 Rotate(const Quat& q, const Vec3& v) noexcept
{
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = Cross(qv, v) * 2.0f;
    return v + t * q.w + Cross(qv, t);
}

/// out[i] = m * (in[i], 1), dropping w. `out` must be at least as long as `in`; they may alias.
inline void TransformPoints(const Mat4& m, std::span<const Vec3> in, std::span<Vec3> out) noexcept
{
    INTERI_ASSERT(out.size() >= in.size());
    const Vec4& c0 = m.columns[0];
    const Vec4& c1 = m.columns[1];
    const Vec4& c2 = m.columns[2];
    const Vec4& c3 = m.columns[3];
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const Vec3 p = in[i];
        out[i] = {((c0.x * p.x + c1.x * p.y) + c2.x * p.z) + c3.x, ((c0.y * p.x + c1.y * p.y) + c2.y * p.z) + c3.y,
                  ((c0.z * p.x + c1.z * p.y) + c2.z * p.z) + c3.z};
    }
}

} // namespace scalar

#if INTERI_HAS_SIMD_KERNELS
namespace simd
{

namespace detail
{
[[nodiscard]] INTERI_FORCE_INLINE __m128 Load(const Vec4& v) noexcept
{
    return _mm_loadu_ps(&v.x);
}
[[nodiscard]] INTERI_FORCE_INLINE __m128 Load(const Quat& q) noexcept
{
    return _mm_loadu_ps(&q.x);
}
/// (x, y, z, 0) without reading past the 12-byte Vec3.
[[nodiscard]] INTERI_FORCE_INLINE __m128 Load(const Vec3& v) noexcept
{
    return _mm_setr_ps(v.x, v.y, v.z, 0.0f);
}
[[nodiscard]] INTERI_FORCE_INLINE Vec4 StoreVec4(__m128 r) noexcept
{
    Vec4 v;
    _mm_storeu_ps(&v.x, r);
    return v;
}
/// Writes exactly 12 bytes.
INTERI_FORCE_INLINE void StoreVec3(__m128 r, Vec3& out) noexcept
{
    out.x = _mm_cvtss_f32(r);
    out.y = _mm_cvtss_f32(_mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 1, 1, 1)));
    out.z = _mm_cvtss_f32(_mm_shuffle_ps(r, r, _MM_SHUFFLE(2, 2, 2, 2)));
}
template <int Lane>
[[nodiscard]] INTERI_FORCE_INLINE __m128 Splat(__m128 r) noexcept
{
    return _mm_shuffle_ps(r, r, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
}
/// Cross product of the xyz lanes; lane 3 of the result is 0 for finite inputs.
[[nodiscard]] INTERI_FORCE_INLINE __m128 Cross3(__m128 a, __m128 b) noexcept
{
    const __m128 aYzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
    const __m128 bYzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
    const __m128 aZxy = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
    const __m128 bZxy = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
    return _mm_sub_ps(_mm_mul_ps(aYzx, bZxy), _mm_mul_ps(aZxy, bYzx));
}
/// x + y + z of a, in lane 0.
[[nodiscard]] INTERI_FORCE_INLINE float Dot3(__m128 a, __m128 b) noexcept
{
    const __m128 m = _mm_mul_ps(a, b);
    return (_mm_cvtss_f32(m) + _mm_cvtss_f32(Splat<1>(m))) + _mm_cvtss_f32(Splat<2>(m));
}
/// c0*x + c1*y + c2*z + c3*w with the same association as scalar::Multiply.
[[nodiscard]] INTERI_FORCE_INLINE __m128 Combine(__m128 c0, __m128 c1, __m128 c2, __m128 c3, __m128 v) noexcept
{
    __m128 r = _mm_add_ps(_mm_mul_ps(c0, Splat<0>(v)), _mm_mul_ps(c1, Splat<1>(v)));
    r = _mm_add_ps(r, _mm_mul_ps(c2, Splat<2>(v)));
    return _mm_add_ps(r, _mm_mul_ps(c3, Splat<3>(v)));
}
} // namespace detail

[[nodiscard]] inline Vec4 Multiply(const Mat4& m, const Vec4& v) noexcept
{
    using namespace detail;
    return StoreVec4(Combine(Load(m.columns[0]), Load(m.columns[1]), Load(m.columns[2]), Load(m.columns[3]), Load(v)));
}

[[nodiscard]] inline Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept
{
    using namespace detail;
    const __m128 c0 = Load(a.columns[0]);
    const __m128 c1 = Load(a.columns[1]);
    const __m128 c2 = Load(a.columns[2]);
    const __m128 c3 = Load(a.columns[3]);
    Mat4 result;
    for (int i = 0; i < 4; ++i)
    {
        result.columns[i] = StoreVec4(Combine(c0, c1, c2, c3, Load(b.columns[i])));
    }
    return result;
}

[[nodiscard]] inline float Determinant(const Mat4& m) noexcept
{
    using namespace detail;
    const __m128 a = Load(m.columns[0]); // lane 3 holds the bottom-row element x
    const __m128 b = Load(m.columns[1]); // ... y
    const __m128 c = Load(m.columns[2]); // ... z
    const __m128 d = Load(m.columns[3]); // ... w
    const __m128 s = Cross3(a, b);
    const __m128 t = Cross3(c, d);
    const __m128 u = _mm_sub_ps(_mm_mul_ps(a, Splat<3>(b)), _mm_mul_ps(b, Splat<3>(a)));
    const __m128 v = _mm_sub_ps(_mm_mul_ps(c, Splat<3>(d)), _mm_mul_ps(d, Splat<3>(c)));
    return Dot3(s, v) + Dot3(t, u);
}

/// Same algorithm as scalar::Inverse, four lanes at a time.
[[nodiscard]] inline Mat4 Inverse(const Mat4& m) noexcept
{
    using namespace detail;
    const __m128 a = Load(m.columns[0]);
    const __m128 b = Load(m.columns[1]);
    const __m128 c = Load(m.columns[2]);
    const __m128 d = Load(m.columns[3]);
    const __m128 x = Splat<3>(a);
    const __m128 y = Splat<3>(b);
    const __m128 z = Splat<3>(c);
    const __m128 w = Splat<3>(d);

    __m128 s = Cross3(a, b);
    __m128 t = Cross3(c, d);
    __m128 u = _mm_sub_ps(_mm_mul_ps(a, y), _mm_mul_ps(b, x)); // lane 3: x*y - y*x = 0
    __m128 v = _mm_sub_ps(_mm_mul_ps(c, w), _mm_mul_ps(d, z));
    const __m128 invDet = _mm_set1_ps(1.0f / (Dot3(s, v) + Dot3(t, u)));
    s = _mm_mul_ps(s, invDet);
    t = _mm_mul_ps(t, invDet);
    u = _mm_mul_ps(u, invDet);
    v = _mm_mul_ps(v, invDet);

    __m128 r0 = _mm_add_ps(Cross3(b, v), _mm_mul_ps(t, y));
    __m128 r1 = _mm_sub_ps(Cross3(v, a), _mm_mul_ps(t, x));
    __m128 r2 = _mm_add_ps(Cross3(d, u), _mm_mul_ps(s, w));
    __m128 r3 = _mm_sub_ps(Cross3(u, c), _mm_mul_ps(s, z));
    // The rows' fourth elements; the cross products leave lane 3 at zero, so the dots must ignore it.
    r0 = _mm_insert_ps(r0, _mm_set_ss(-Dot3(b, t)), 0x30);
    r1 = _mm_insert_ps(r1, _mm_set_ss(Dot3(a, t)), 0x30);
    r2 = _mm_insert_ps(r2, _mm_set_ss(-Dot3(d, s)), 0x30);
    r3 = _mm_insert_ps(r3, _mm_set_ss(Dot3(c, s)), 0x30);
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3); // rows -> columns
    return {{StoreVec4(r0), StoreVec4(r1), StoreVec4(r2), StoreVec4(r3)}};
}

[[nodiscard]] inline Quat Multiply(const Quat& qa, const Quat& qb) noexcept
{
    using namespace detail;
    const __m128 a = Load(qa);
    const __m128 b = Load(qb);
    // result = aw*(bx,by,bz,bw) + ax*(bw,-bz,by,-bx) + ay*(bz,bw,-bx,-by) + az*(-by,bx,bw,-bz)
    const __m128 signs1 = _mm_setr_ps(1.0f, -1.0f, 1.0f, -1.0f);
    const __m128 signs2 = _mm_setr_ps(1.0f, 1.0f, -1.0f, -1.0f);
    const __m128 signs3 = _mm_setr_ps(-1.0f, 1.0f, 1.0f, -1.0f);
    const __m128 b1 = _mm_mul_ps(_mm_shuffle_ps(b, b, _MM_SHUFFLE(0, 1, 2, 3)), signs1);
    const __m128 b2 = _mm_mul_ps(_mm_shuffle_ps(b, b, _MM_SHUFFLE(1, 0, 3, 2)), signs2);
    const __m128 b3 = _mm_mul_ps(_mm_shuffle_ps(b, b, _MM_SHUFFLE(2, 3, 0, 1)), signs3);
    __m128 r = _mm_add_ps(_mm_mul_ps(Splat<3>(a), b), _mm_mul_ps(Splat<0>(a), b1));
    r = _mm_add_ps(r, _mm_mul_ps(Splat<1>(a), b2));
    r = _mm_add_ps(r, _mm_mul_ps(Splat<2>(a), b3));
    Quat result;
    _mm_storeu_ps(&result.x, r);
    return result;
}

[[nodiscard]] inline Vec3 Rotate(const Quat& q, const Vec3& v) noexcept
{
    using namespace detail;
    const __m128 qv = Load(q);
    const __m128 p = Load(v);
    const __m128 t = _mm_mul_ps(Cross3(qv, p), _mm_set1_ps(2.0f));
    const __m128 r = _mm_add_ps(_mm_add_ps(p, _mm_mul_ps(t, Splat<3>(qv))), Cross3(qv, t));
    Vec3 result;
    StoreVec3(r, result);
    return result;
}

inline void TransformPoints(const Mat4& m, std::span<const Vec3> in, std::span<Vec3> out) noexcept
{
    using namespace detail;
    INTERI_ASSERT(out.size() >= in.size());
    const __m128 c0 = Load(m.columns[0]);
    const __m128 c1 = Load(m.columns[1]);
    const __m128 c2 = Load(m.columns[2]);
    const __m128 c3 = Load(m.columns[3]);
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const Vec3& p = in[i];
        __m128 r = _mm_add_ps(_mm_mul_ps(c0, _mm_set1_ps(p.x)), _mm_mul_ps(c1, _mm_set1_ps(p.y)));
        r = _mm_add_ps(r, _mm_mul_ps(c2, _mm_set1_ps(p.z)));
        StoreVec3(_mm_add_ps(r, c3), out[i]);
    }
}

} // namespace simd

namespace active = simd;
#else
namespace active = scalar;
#endif

} // namespace interi::math

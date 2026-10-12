// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/core/math/ConvexHull.h>
#include <interi/core/math/MathKernels.h>
#include <interi/core/math/Quat.h>

#include <benchmark/benchmark.h>

#include <vector>

namespace
{
namespace scalar = interi::math::scalar;
using interi::Mat4;
using interi::Quat;
using interi::Vec3;

Mat4 TestMatrix(float seed)
{
    return Mat4::FromTRS(Vec3{seed, 2, 3}, Quat::FromAxisAngle(interi::Normalize(Vec3{1, seed, 0.5f}), seed),
                         Vec3{1.5f, 0.75f, 2});
}

template <typename Kernel>
void Mat4Multiply(benchmark::State& state, Kernel kernel)
{
    Mat4 a = TestMatrix(0.3f);
    const Mat4 b = TestMatrix(0.7f);
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(a = kernel(a, b));
    }
}

template <typename Kernel>
void Mat4Inverse(benchmark::State& state, Kernel kernel)
{
    Mat4 m = TestMatrix(0.4f);
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(m = kernel(m));
    }
}

template <typename Kernel>
void QuatMultiply(benchmark::State& state, Kernel kernel)
{
    Quat a = Quat::FromAxisAngle(Vec3::UnitX(), 0.1f);
    const Quat b = Quat::FromAxisAngle(interi::Normalize(Vec3{1, 1, 0}), 0.2f);
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(a = kernel(a, b));
    }
}

template <typename Kernel>
void TransformPoints(benchmark::State& state, Kernel kernel)
{
    const Mat4 m = TestMatrix(0.5f);
    std::vector<Vec3> points(4096, Vec3{1, 2, 3});
    std::vector<Vec3> out(points.size());
    for (auto _ : state)
    {
        kernel(m, points, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(points.size()));
}

void BM_Mat4Multiply_Scalar(benchmark::State& state)
{
    Mat4Multiply(state, [](const Mat4& a, const Mat4& b) { return scalar::Multiply(a, b); });
}
BENCHMARK(BM_Mat4Multiply_Scalar);
void BM_Mat4Inverse_Scalar(benchmark::State& state)
{
    Mat4Inverse(state, [](const Mat4& m) { return scalar::Inverse(m); });
}
BENCHMARK(BM_Mat4Inverse_Scalar);
void BM_QuatMultiply_Scalar(benchmark::State& state)
{
    QuatMultiply(state, [](const Quat& a, const Quat& b) { return scalar::Multiply(a, b); });
}
BENCHMARK(BM_QuatMultiply_Scalar);
void BM_TransformPoints_Scalar(benchmark::State& state)
{
    TransformPoints(state, [](const Mat4& m, std::span<const Vec3> in, std::span<Vec3> out)
                    { scalar::TransformPoints(m, in, out); });
}
BENCHMARK(BM_TransformPoints_Scalar);

#if INTERI_HAS_SIMD_KERNELS
namespace simd = interi::math::simd;
void BM_Mat4Multiply_Simd(benchmark::State& state)
{
    Mat4Multiply(state, [](const Mat4& a, const Mat4& b) { return simd::Multiply(a, b); });
}
BENCHMARK(BM_Mat4Multiply_Simd);
void BM_Mat4Inverse_Simd(benchmark::State& state)
{
    Mat4Inverse(state, [](const Mat4& m) { return simd::Inverse(m); });
}
BENCHMARK(BM_Mat4Inverse_Simd);
void BM_QuatMultiply_Simd(benchmark::State& state)
{
    QuatMultiply(state, [](const Quat& a, const Quat& b) { return simd::Multiply(a, b); });
}
BENCHMARK(BM_QuatMultiply_Simd);
void BM_TransformPoints_Simd(benchmark::State& state)
{
    TransformPoints(state, [](const Mat4& m, std::span<const Vec3> in, std::span<Vec3> out)
                    { simd::TransformPoints(m, in, out); });
}
BENCHMARK(BM_TransformPoints_Simd);
#endif

void BM_ConvexHull_SpherePoints(benchmark::State& state)
{
    std::vector<Vec3> points;
    std::uint64_t seed = 1;
    for (std::int64_t i = 0; i < state.range(0); ++i)
    {
        auto next = [&] { return static_cast<float>((seed = interi::MixBits(seed)) >> 40) / 16777216.0f - 0.5f; };
        points.push_back(interi::Normalize(Vec3{next(), next(), next()}));
    }
    for (auto _ : state)
    {
        auto hull = interi::BuildConvexHull(points);
        benchmark::DoNotOptimize(hull);
    }
}
BENCHMARK(BM_ConvexHull_SpherePoints)->Arg(64)->Arg(1024);
} // namespace

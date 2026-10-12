// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

// INTERI_TEST_SIMD_REQUESTED mirrors the INTERI_SIMD CMake option. On an x64 compiler the engine-wide SIMD
// defines must then be present; if CMake's processor detection ever misses (e.g. a new spelling of the
// architecture name), every build would silently fall back to scalar code while all other tests stay green.
TEST(BuildConfig, X64WithSimdRequested_DefinesSimdMacros)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (INTERI_TEST_SIMD_REQUESTED == 0)
    {
        GTEST_SKIP() << "INTERI_SIMD is OFF for this build";
    }
#if !defined(INTERI_SIMD_SSE4) || !defined(INTERI_SIMD_AVX2)
    FAIL() << "x64 build without INTERI_SIMD_SSE4/INTERI_SIMD_AVX2: check cmake/InteriArch.cmake";
#endif
#else
    GTEST_SKIP() << "not an x86-64 build";
#endif
}

TEST(BuildConfig, AvxBaseline_IsEnabledByCompilerFlags)
{
#if defined(INTERI_SIMD_AVX2) && (defined(__clang__) || defined(__GNUC__))
    // Clang/GCC define these only when -march grants the ISA; MSVC has no equivalent for FMA.
#if !defined(__AVX2__) || !defined(__FMA__)
    FAIL() << "INTERI_SIMD_AVX2 is set but the compiler was not given the x86-64-v3 ISA";
#endif
#elif defined(INTERI_SIMD_AVX2) && defined(_MSC_VER)
#if !defined(__AVX2__)
    FAIL() << "INTERI_SIMD_AVX2 is set but MSVC was not given /arch:AVX2";
#endif
#else
    GTEST_SKIP() << "SIMD disabled";
#endif
}

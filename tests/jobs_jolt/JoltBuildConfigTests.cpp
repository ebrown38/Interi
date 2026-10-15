// SPDX-License-Identifier: MIT
// Jolt is built by the overlay port in cmake/ports/joltphysics. These pin its exported configuration to the engine's
// CPU baseline (x86-64-v3, cmake/InteriArch.cmake) and to cross-platform determinism.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <gtest/gtest.h>

TEST(JoltBuildConfig, CrossPlatformDeterministic_Enabled)
{
#ifndef JPH_CROSS_PLATFORM_DETERMINISTIC
    ADD_FAILURE() << "Jolt must be built with CROSS_PLATFORM_DETERMINISTIC=ON (overlay port cmake/ports/joltphysics)";
#endif
}

TEST(JoltBuildConfig, Isa_MatchesEngineBaseline)
{
#if defined(INTERI_SIMD_AVX2)
#if !defined(JPH_USE_AVX2) || !defined(JPH_USE_SSE4_2) || !defined(JPH_USE_LZCNT) || !defined(JPH_USE_TZCNT) ||        \
    !defined(JPH_USE_F16C)
    ADD_FAILURE() << "Jolt's ISA options are below the engine's x86-64-v3 baseline";
#endif
#endif
    // Cross-platform determinism must turn off Jolt's FMA paths: MSVC and Clang would round differently otherwise.
#ifdef JPH_USE_FMADD
    ADD_FAILURE() << "JPH_USE_FMADD is set, so Jolt results are not bit-identical across compilers";
#endif
}

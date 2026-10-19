# SPDX-License-Identifier: MIT
# Engine-wide CPU baseline and floating-point settings. Applied as directory-wide compile options so every Interi
# target (libraries, tests, benchmarks, tools) agrees on the ISA: inline SIMD in headers must compile identically
# everywhere, and libraries linked in (a physics engine, say) can export AVX2/FMA requirements of their own.
#
# Baseline: x86-64-v3 (AVX2, FMA, F16C, BMI1/2, LZCNT), i.e. Haswell / Zen 1 and newer, matching PS5 and
# Xbox Series (Zen 2). The engine's math code tests INTERI_SIMD_* (set here), never compiler-specific macros such
# as __SSE4_1__, which MSVC does not define.

option(INTERI_SIMD "Use SIMD (x86-64-v3) code paths; OFF builds the scalar fallback everywhere" ON)

set(INTERI_X86_64 OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(INTERI_X86_64 ON)
endif()

# INTERI_ABI_COMPILE_OPTIONS / _DEFINITIONS / _LINK_OPTIONS collect the global flags an out-of-tree module must share
# (see InteriBuildSettings.cmake).
if(INTERI_SIMD AND INTERI_X86_64)
    if(MSVC)
        list(APPEND INTERI_ABI_COMPILE_OPTIONS /arch:AVX2)
    else()
        list(APPEND INTERI_ABI_COMPILE_OPTIONS -march=x86-64-v3)
    endif()
    list(APPEND INTERI_ABI_DEFINITIONS INTERI_SIMD_SSE4=1 INTERI_SIMD_AVX2=1)
endif()

# Never let the compiler fuse a*b+c into an FMA on its own: Clang contracts by default when FMA is available and
# MSVC does not, so results (and determinism hashes) would differ between platforms. Explicit FMA intrinsics are
# still allowed where the code asks for them.
if(NOT MSVC)
    list(APPEND INTERI_ABI_COMPILE_OPTIONS -ffp-contract=off)
endif()

add_compile_options(${INTERI_ABI_COMPILE_OPTIONS})
add_compile_definitions(${INTERI_ABI_DEFINITIONS})

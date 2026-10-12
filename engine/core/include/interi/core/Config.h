// SPDX-License-Identifier: MIT
#pragma once

/// Build configuration switches shared by every engine module.
///
/// INTERI_SHIPPING: defined for final release builds. Asserts and profiling are compiled out.
/// INTERI_ENABLE_ASSERTS: 1 unless shipping. Override with -DINTERI_ENABLE_ASSERTS=0/1.
///   Asserts stay enabled in RelWithDebInfo so release-preset CI still catches violations.

#if !defined(INTERI_ENABLE_ASSERTS)
#if defined(INTERI_SHIPPING)
#define INTERI_ENABLE_ASSERTS 0
#else
#define INTERI_ENABLE_ASSERTS 1
#endif
#endif

#if defined(_MSC_VER)
#define INTERI_FORCE_INLINE __forceinline
/// Disables one MSVC warning until INTERI_MSVC_WARNING_POP(), e.g. C4324 (padding added for alignas).
#define INTERI_MSVC_WARNING_PUSH_DISABLE(number) __pragma(warning(push)) __pragma(warning(disable : number))
#define INTERI_MSVC_WARNING_POP() __pragma(warning(pop))
#else
#define INTERI_FORCE_INLINE inline __attribute__((always_inline))
#define INTERI_MSVC_WARNING_PUSH_DISABLE(number)
#define INTERI_MSVC_WARNING_POP()
#endif

#include <cstddef>

namespace interi
{

/// Size used to separate data written by different threads (avoids false sharing). Hardcoded rather than
/// std::hardware_destructive_interference_size, whose value differs between compilers and warns on GCC/Clang.
inline constexpr std::size_t kCacheLineSize = 64;

} // namespace interi

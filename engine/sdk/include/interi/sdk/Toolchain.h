// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

// The toolchain fingerprint: everything that must match for two binaries to share C++ types (standard library
// containers, vtables, class layouts) across a shared-library boundary. A module is built against the engine from
// source, and the loader rejects a module whose fingerprint differs from the host's.
//
// It is assembled by the preprocessor in this header, so each binary reports the toolchain that compiled it: compiler
// and major.minor version, standard library and its debug mode, C++ standard, module ABI version, CPU baseline, and
// the Interi build switches that change public layouts or behaviour (shipping, profiling, shared engine).

#define INTERI_DETAIL_STRINGIFY_INNER(x) #x
#define INTERI_DETAIL_STRINGIFY(x) INTERI_DETAIL_STRINGIFY_INNER(x)

/// Bumped whenever the module ABI (ModuleApi.h, the registrar, hooks) changes.
#define INTERI_MODULE_ABI_VERSION 7 // NOLINT(modernize-macro-to-enum): stringified below

#if defined(__clang__)
#define INTERI_DETAIL_FP_COMPILER                                                                                      \
    "clang-" INTERI_DETAIL_STRINGIFY(__clang_major__) "." INTERI_DETAIL_STRINGIFY(__clang_minor__)
#elif defined(_MSC_VER)
#define INTERI_DETAIL_FP_COMPILER "msvc-" INTERI_DETAIL_STRINGIFY(_MSC_VER)
#elif defined(__GNUC__)
#define INTERI_DETAIL_FP_COMPILER "gcc-" INTERI_DETAIL_STRINGIFY(__GNUC__) "." INTERI_DETAIL_STRINGIFY(__GNUC_MINOR__)
#else
#define INTERI_DETAIL_FP_COMPILER "unknown-compiler"
#endif

// <version> defines the standard library macros below.
#include <version>

#if defined(_LIBCPP_VERSION)
#define INTERI_DETAIL_FP_STDLIB "libc++-" INTERI_DETAIL_STRINGIFY(_LIBCPP_VERSION)
#elif defined(_GLIBCXX_RELEASE)
#define INTERI_DETAIL_FP_STDLIB "libstdc++-" INTERI_DETAIL_STRINGIFY(_GLIBCXX_RELEASE)
#elif defined(_MSVC_STL_VERSION)
#define INTERI_DETAIL_FP_STDLIB "msstl-" INTERI_DETAIL_STRINGIFY(_MSVC_STL_VERSION)
#else
#define INTERI_DETAIL_FP_STDLIB "unknown-stdlib"
#endif

// Debug modes change container layouts: MSVC's iterator debug level and debug CRT, libstdc++'s debug mode.
#if defined(_MSC_VER) && !defined(__clang__)
#if defined(_DEBUG)
#define INTERI_DETAIL_FP_DEBUG "idl" INTERI_DETAIL_STRINGIFY(_ITERATOR_DEBUG_LEVEL) "-dcrt"
#else
#define INTERI_DETAIL_FP_DEBUG "idl" INTERI_DETAIL_STRINGIFY(_ITERATOR_DEBUG_LEVEL)
#endif
#elif defined(_GLIBCXX_DEBUG)
#define INTERI_DETAIL_FP_DEBUG "glibcxx-debug"
#else
#define INTERI_DETAIL_FP_DEBUG "nodebug"
#endif

#if defined(_MSVC_LANG)
#define INTERI_DETAIL_FP_STD "cxx" INTERI_DETAIL_STRINGIFY(_MSVC_LANG)
#else
#define INTERI_DETAIL_FP_STD "cxx" INTERI_DETAIL_STRINGIFY(__cplusplus)
#endif

#if defined(INTERI_SIMD_AVX2)
#define INTERI_DETAIL_FP_ARCH "avx2"
#else
#define INTERI_DETAIL_FP_ARCH "scalar"
#endif

#if defined(INTERI_SHIPPING)
#define INTERI_DETAIL_FP_SHIPPING "shipping"
#else
#define INTERI_DETAIL_FP_SHIPPING "dev"
#endif

// A module can only share the engine when the engine is a set of shared libraries.
#if defined(INTERI_SHARED_ENGINE)
#define INTERI_DETAIL_FP_LINKAGE "shared"
#else
#define INTERI_DETAIL_FP_LINKAGE "static"
#endif

#if defined(INTERI_ENABLE_PROFILING)
#define INTERI_DETAIL_FP_PROFILING "profiling"
#else
#define INTERI_DETAIL_FP_PROFILING "noprofiling"
#endif

/// The fingerprint of the binary that includes this header, as a string literal.
#define INTERI_TOOLCHAIN_FINGERPRINT                                                                                   \
    "abi" INTERI_DETAIL_STRINGIFY(INTERI_MODULE_ABI_VERSION) ";" INTERI_DETAIL_FP_COMPILER ";" INTERI_DETAIL_FP_STDLIB \
                                                             ";" INTERI_DETAIL_FP_DEBUG ";" INTERI_DETAIL_FP_STD       \
                                                             ";" INTERI_DETAIL_FP_ARCH ";" INTERI_DETAIL_FP_SHIPPING   \
                                                             ";" INTERI_DETAIL_FP_PROFILING                            \
                                                             ";" INTERI_DETAIL_FP_LINKAGE

namespace interi::sdk
{

/// This binary's toolchain fingerprint (see the top of this header).
inline constexpr std::string_view kToolchainFingerprint = INTERI_TOOLCHAIN_FINGERPRINT;

/// The fingerprint the engine's sdk library was compiled with: what the host compares modules against.
[[nodiscard]] std::string_view EngineToolchainFingerprint() noexcept;

} // namespace interi::sdk

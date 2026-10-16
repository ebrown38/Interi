// SPDX-License-Identifier: MIT
// Compiled into every test executable that links interi::vulkan_testing: sanitizer settings for running on Mesa's
// lavapipe, whose code is not instrumented and is not ours.

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define INTERI_TEST_ASAN 1
#endif
#if __has_feature(thread_sanitizer)
#define INTERI_TEST_TSAN 1
#endif
#endif

#ifdef INTERI_TEST_ASAN
#include <dlfcn.h>

namespace
{

/// Keeps lavapipe (and the LLVM it JITs shaders with) loaded until exit. Otherwise the Vulkan loader unloads it
/// when the last instance is destroyed, and LeakSanitizer's exit-time report shows its allocations as
/// "<unknown module>", which no suppression can match.
[[maybe_unused]] const void* const g_pinLavapipe = dlopen("libvulkan_lvp.so", RTLD_NOW | RTLD_NODELETE);

} // namespace

/// LeakSanitizer suppressions: process-lifetime allocations of the Vulkan loader, Mesa and LLVM (made once, e.g.
/// under pthread_once or by Mesa's shader-compile threads, and never freed).
extern "C" const char* __lsan_default_suppressions() // NOLINT(bugprone-reserved-identifier)
{
    return "leak:libvulkan.so\n"
           "leak:libvulkan_lvp.so\n"
           "leak:libLLVM\n";
}
#endif

#ifdef INTERI_TEST_TSAN
/// ThreadSanitizer suppressions: lavapipe's rasterizer, queue and shader-compile threads synchronise inside
/// uninstrumented code (Mesa, LLVM, the loader and the validation layer), so TSan sees only part of each
/// synchronisation and reports races and lock-order inversions that are not there. Reports are suppressed only
/// when a frame of the report is in one of those libraries; interceptors stay active so lock tracking is
/// consistent. Executables that also run Jolt (interi::physics_testing) get its suppression here, because a
/// process has only one list (see tests/physics/JoltSanitizerSupport.cpp).
extern "C" const char* __tsan_default_suppressions() // NOLINT(bugprone-reserved-identifier)
{
    return
#ifdef INTERI_PHYSICS_TESTING
        "race:JPH::Mutex::try_lock\n"
#endif
        "race:libvulkan_lvp.so\n"
        "race:libLLVM\n"
        "race:libVkLayer_khronos_validation.so\n"
        "deadlock:libvulkan_lvp.so\n"
        "deadlock:libLLVM\n"
        "deadlock:libvulkan.so\n";
}
#endif

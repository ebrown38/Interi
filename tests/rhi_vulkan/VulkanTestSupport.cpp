// SPDX-License-Identifier: MIT
// Compiled into every test executable that links interi::vulkan_testing.

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define INTERI_TEST_LSAN 1
#endif
#endif

#ifdef INTERI_TEST_LSAN
/// LeakSanitizer suppressions. Mesa's drivers keep process-lifetime allocations (made once under pthread_once and
/// never freed), reached through the Vulkan loader; they are not ours.
extern "C" const char* __lsan_default_suppressions() // NOLINT(bugprone-reserved-identifier)
{
    return "leak:libvulkan.so\n";
}
#endif

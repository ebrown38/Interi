// SPDX-License-Identifier: MIT
// Compiled into every executable that links interi::physics_testing: sanitizer settings for Jolt.

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define INTERI_TEST_TSAN 1
#endif
#endif

// With interi::vulkan_testing too, VulkanTestSupport.cpp defines the (single) suppression list and includes these.
#if defined(INTERI_TEST_TSAN) && !defined(INTERI_VULKAN_TESTING)
/// ThreadSanitizer suppressions. Jolt's Mutex::try_lock reads the owner thread id, a debug-only field kept for
/// JPH_ASSERT (the tsan triplet builds Jolt with asserts), without holding the lock, so heavily contended body locks
/// (many bodies falling asleep in one step) report a race on that field. The mutex itself is correct.
extern "C" const char* __tsan_default_suppressions() // NOLINT(bugprone-reserved-identifier)
{
    return "race:JPH::Mutex::try_lock\n";
}
#endif

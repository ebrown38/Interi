// SPDX-License-Identifier: MIT
// A process has one ThreadSanitizer suppression list. This executable runs Jolt and lavapipe, so the list must hold
// both sets (see tests/rhi_vulkan/VulkanTestSupport.cpp, INTERI_EXTRA_TSAN_SUPPRESSIONS).
#include <gtest/gtest.h>

#include <string>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define INTERI_TEST_TSAN_BUILD 1
#endif
#endif

#ifdef INTERI_TEST_TSAN_BUILD
extern "C" const char* __tsan_default_suppressions(); // NOLINT(bugprone-reserved-identifier)
#endif

TEST(TsanSuppressions, JoltAndLavapipe_BothInTheOneList)
{
#ifdef INTERI_TEST_TSAN_BUILD
    const std::string list = __tsan_default_suppressions();
    EXPECT_NE(list.find("race:JPH::Mutex::try_lock\n"), std::string::npos);
    EXPECT_NE(list.find("race:libvulkan_lvp.so\n"), std::string::npos);
#else
    GTEST_SKIP() << "only meaningful under ThreadSanitizer";
#endif
}

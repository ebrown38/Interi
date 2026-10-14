// SPDX-License-Identifier: MIT
#pragma once

#include <interi/rhi_vulkan/VulkanDevice.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <utility>

namespace interi::testing
{

/// std::getenv without MSVC's deprecation warning (read-only use during single-threaded test setup).
inline const char* GetEnv(const char* name)
{
#if defined(_MSC_VER)
#pragma warning(suppress : 4996)
    return std::getenv(name);
#else
    return std::getenv(name);
#endif
}

/// Points the Vulkan loader at lavapipe (when CMake found it) unless VK_DRIVER_FILES is already set.
inline void UseLavapipeIfAvailable()
{
#ifdef INTERI_LAVAPIPE_ICD
    if (GetEnv("VK_DRIVER_FILES") == nullptr)
    {
#ifdef _WIN32
        _putenv_s("VK_DRIVER_FILES", INTERI_LAVAPIPE_ICD);
#else
        setenv("VK_DRIVER_FILES", INTERI_LAVAPIPE_ICD, 0);
#endif
    }
#endif
}

/// Fixture for tests on a real Vulkan device with validation enabled. Skips when the machine has no Vulkan 1.3
/// device, unless INTERI_REQUIRE_GPU is set (CI on Linux, where lavapipe must be found). Fails the test if the
/// validation layer reported an error.
class VulkanTest : public ::testing::Test
{
protected:
    void SetUp() override { CreateDevice(rhi::VulkanDeviceDesc{.validation = true}); }

    /// Creates `device`, or skips/fails the test as described above. For fixtures needing other options.
    void CreateDevice(const rhi::VulkanDeviceDesc& desc)
    {
        UseLavapipeIfAvailable();
        auto created = rhi::VulkanDevice::Create(desc);
        if (!created.has_value())
        {
            if (GetEnv("INTERI_REQUIRE_GPU") != nullptr)
            {
                FAIL() << "INTERI_REQUIRE_GPU is set but no device: " << created.error().message;
            }
            GTEST_SKIP() << "no Vulkan device: " << created.error().message;
        }
        device = std::move(*created);
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            EXPECT_EQ(device->ValidationErrorCount(), 0u) << "Vulkan validation errors (see stderr)";
        }
        device.reset();
    }

    std::unique_ptr<rhi::VulkanDevice> device;
};

} // namespace interi::testing

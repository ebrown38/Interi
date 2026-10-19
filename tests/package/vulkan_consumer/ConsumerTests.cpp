// SPDX-License-Identifier: MIT
#include <interi/editor_ui/EditorExtensionsApi.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

using namespace interi;

class Consumer : public interi::testing::VulkanTest
{
};

TEST_F(Consumer, Device_HasAName)
{
    EXPECT_FALSE(device->DeviceName().empty());
}

TEST(VulkanConsumer, EditorApi_RenderFeatureDefaultsAreEmpty)
{
    const editor::RenderFeatureDef def;
    EXPECT_FALSE(def.render);
    EXPECT_FALSE(def.contribute);
}

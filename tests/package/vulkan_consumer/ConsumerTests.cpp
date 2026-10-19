// SPDX-License-Identifier: MIT
// Links the Vulkan component's libraries from an installed engine. It never creates a device (machines without a
// driver must pass), and needs no GoogleTest: the point is that the package's targets, headers and dependencies
// resolve.
#include <interi/editor_ui/EditorExtensionsApi.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include <cstdio>

int main()
{
    using namespace interi;
    // Taking the addresses forces the linker to resolve the backend and the platform from the installed libraries.
    const auto createDevice = &rhi::VulkanDevice::Create;
    const auto createPlatform = &SdlPlatform::Create;
    const editor::RenderFeatureDef def;
    if (createDevice == nullptr || createPlatform == nullptr || def.render || def.contribute)
    {
        std::puts("unexpected package contents");
        return 1;
    }
    return 0;
}

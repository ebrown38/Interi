# SPDX-License-Identifier: MIT
# interi::vulkan_testing for modules (find_package(Interi COMPONENTS Vulkan)): what the engine's GPU tests share. Link it
# into a test executable that also links GTest. It adds the VulkanTest fixture (VulkanTestDevice.h), the golden-image
# helpers (GoldenImage.h, RenderScene.h, OffscreenTarget.h) and VulkanTestSupport.cpp, which selects Mesa's lavapipe when
# installed and carries the sanitizer suppressions (a TSan list can be extended through
# INTERI_EXTRA_TSAN_SUPPRESSIONS, see that file).
get_filename_component(_interi_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(_interi_gpu_testing "${_interi_prefix}/share/interi/gpu_testing")
find_file(INTERI_LAVAPIPE_ICD lvp_icd.x86_64.json PATHS /usr/share/vulkan/icd.d NO_DEFAULT_PATH)
if(NOT TARGET interi::vulkan_testing)
    add_library(interi::vulkan_testing INTERFACE IMPORTED)
    set_property(TARGET interi::vulkan_testing PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_interi_gpu_testing}")
    set_property(TARGET interi::vulkan_testing PROPERTY INTERFACE_SOURCES "${_interi_gpu_testing}/VulkanTestSupport.cpp")
    set_property(TARGET interi::vulkan_testing PROPERTY INTERFACE_COMPILE_DEFINITIONS INTERI_VULKAN_TESTING=1)
    if(INTERI_LAVAPIPE_ICD)
        set_property(TARGET interi::vulkan_testing APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
            "INTERI_LAVAPIPE_ICD=\"${INTERI_LAVAPIPE_ICD}\"")
    endif()
    set_property(TARGET interi::vulkan_testing PROPERTY INTERFACE_LINK_LIBRARIES interi::rhi_vulkan)
endif()

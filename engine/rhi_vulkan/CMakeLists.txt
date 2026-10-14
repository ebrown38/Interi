# SPDX-License-Identifier: MIT
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)

set(_tests ${PROJECT_SOURCE_DIR}/tests/rhi_vulkan)

# Shared by every test executable that runs on a real Vulkan device (VulkanTestDevice.h). When Mesa's lavapipe
# (software Vulkan) is installed, tests use it instead of whatever GPUs the machine has, so results are the same
# locally and in CI. Setting VK_DRIVER_FILES yourself overrides this.
find_file(INTERI_LAVAPIPE_ICD lvp_icd.x86_64.json PATHS /usr/share/vulkan/icd.d NO_DEFAULT_PATH)
add_library(interi_vulkan_testing INTERFACE)
add_library(interi::vulkan_testing ALIAS interi_vulkan_testing)
target_include_directories(interi_vulkan_testing INTERFACE ${_tests})
target_sources(interi_vulkan_testing INTERFACE ${_tests}/VulkanTestSupport.cpp)
if(INTERI_LAVAPIPE_ICD)
    target_compile_definitions(interi_vulkan_testing INTERFACE INTERI_LAVAPIPE_ICD="${INTERI_LAVAPIPE_ICD}")
endif()

# Vulkan backend for interi::rhi. The loader is loaded at runtime through volk, so nothing links libvulkan, and
# window-system integration (surfaces) comes from the caller: this module never links SDL.
interi_add_module(rhi_vulkan
    SOURCES
        src/ShaderReflection.cpp
        src/VmaImplementation.cpp
        src/VulkanDevice.cpp
    DEPS interi::rhi
    PRIVATE_DEPS volk::volk GPUOpen::VulkanMemoryAllocator unofficial::spirv-reflect
    TEST_DEPS interi::testing interi::vulkan_testing
    TEST_SOURCES
        ${_tests}/ComputeTests.cpp
        ${_tests}/VulkanDeviceTests.cpp
    TEST_LABELS integration)
interi_add_shaders(interi_rhi_vulkan_tests
    SOURCES
        ${_tests}/shaders/MatrixConvention.cs.hlsl
        ${_tests}/shaders/PrefixSum.cs.hlsl)
target_compile_definitions(interi_rhi_vulkan PRIVATE
    VK_NO_PROTOTYPES VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
set_source_files_properties(src/VmaImplementation.cpp PROPERTIES SKIP_LINTING ON)
# Vulkan's C structs are meant to be designated-initialized with the rest zeroed. Source-level options come after
# the interi::warnings flags, so this wins over -Wextra.
if(NOT MSVC)
    set_source_files_properties(src/VulkanDevice.cpp PROPERTIES
        COMPILE_OPTIONS -Wno-missing-designated-field-initializers)
endif()

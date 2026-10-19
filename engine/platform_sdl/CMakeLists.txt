# SPDX-License-Identifier: MIT
find_package(SDL3 CONFIG REQUIRED)

set(_tests ${PROJECT_SOURCE_DIR}/tests/platform_sdl)

# SDL3 backend for the platform interfaces. Only executables that open windows link it; everything else depends
# on interi::platform alone.
interi_add_module(platform_sdl
    SOURCES
        src/SdlPlatform.cpp
    COMPONENT Vulkan
    DEPS interi::platform
    PRIVATE_DEPS SDL3::SDL3
    TEST_DEPS SDL3::SDL3
    TEST_SOURCES
        ${_tests}/SdlPlatformTests.cpp)

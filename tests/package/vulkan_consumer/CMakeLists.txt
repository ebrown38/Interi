# SPDX-License-Identifier: MIT
# Out-of-tree consumer of the package's Vulkan component: a program that links the backend, the SDL3 platform, the
# GPU test helpers and the editor extension API header, the way a module with GPU tests does.
cmake_minimum_required(VERSION 3.29)
project(interi_vulkan_consumer LANGUAGES CXX)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
include(CTest)
find_package(Interi REQUIRED COMPONENTS Vulkan)

add_executable(vulkan_consumer_tests ConsumerTests.cpp)
target_link_libraries(vulkan_consumer_tests PRIVATE interi::rhi_vulkan interi::platform_sdl interi::vulkan_testing
                      interi::editor_api)
add_test(NAME vulkan_consumer_tests COMMAND vulkan_consumer_tests)

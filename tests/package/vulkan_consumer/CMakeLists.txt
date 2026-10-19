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
if(WIN32)
    # The engine's shared libraries sit in the install's bin folder, not next to this executable: without them Windows
    # shows a "DLL not found" dialog that never closes on a CI runner.
    add_custom_command(TARGET vulkan_consumer_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:vulkan_consumer_tests>
                $<TARGET_FILE_DIR:vulkan_consumer_tests>
        COMMAND_EXPAND_LISTS)
endif()
add_test(NAME vulkan_consumer_tests COMMAND vulkan_consumer_tests)

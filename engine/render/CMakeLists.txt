# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/render)

# Renderer built on the backend-neutral RHI: frame graph, passes, materials. Unit tests use tests/rhi/MockDevice.h;
# interi_render_gpu_tests runs the same code on a real Vulkan device.
interi_add_module(render
    SOURCES
        src/FrameGraph.cpp
        src/HeaderCheck.cpp
    DEPS interi::rhi
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/FrameGraphTests.cpp)

interi_add_test_executable(interi_render_gpu_tests
    SOURCES ${_tests}/gpu/FrameGraphGpuTests.cpp
    DEPS interi::render interi::rhi_vulkan interi::vulkan_testing
    LABELS integration)

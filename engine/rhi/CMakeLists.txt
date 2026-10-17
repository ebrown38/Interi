# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/rhi)

# Backend-neutral render hardware interface plus the CPU-side managers built on it. Backends (rhi_vulkan, later
# D3D12 and consoles) implement IDevice; everything here is tested against tests/rhi/MockDevice.h.
interi_add_module(rhi SHARED
    SOURCES
        src/CommandListPool.cpp
        src/HeaderCheck.cpp
        src/ResourceManager.cpp
        src/UploadQueue.cpp
    DEPS interi::core interi::jobs
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CommandListPoolTests.cpp
        ${_tests}/ResourceManagerTests.cpp
        ${_tests}/UploadQueueTests.cpp)

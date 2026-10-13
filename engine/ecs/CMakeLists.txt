# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/ecs)

interi_add_module(ecs
    SOURCES
        src/CommandBuffer.cpp
        src/Component.cpp
        src/HeaderCheck.cpp
        src/World.cpp
    DEPS interi::core interi::jobs
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CommandBufferTests.cpp
        ${_tests}/QueryTests.cpp
        ${_tests}/WorldTests.cpp)

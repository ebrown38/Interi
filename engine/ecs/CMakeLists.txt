# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/ecs)

interi_add_module(ecs
    SOURCES
        src/Component.cpp
        src/HeaderCheck.cpp
        src/World.cpp
    DEPS interi::core interi::jobs
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/WorldTests.cpp)

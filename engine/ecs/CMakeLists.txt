# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/ecs)

interi_add_module(ecs
    SOURCES
        src/CommandBuffer.cpp
        src/Component.cpp
        src/HeaderCheck.cpp
        src/Scheduler.cpp
        src/World.cpp
    DEPS interi::core interi::jobs
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CommandBufferTests.cpp
        ${_tests}/QueryTests.cpp
        ${_tests}/SchedulerTests.cpp
        ${_tests}/WorldTests.cpp)

# Long-running concurrency tests (run under the tsan preset): label `stress`.
interi_add_test_executable(interi_ecs_stress_tests
    SOURCES ${_tests}/stress/SchedulerStressTests.cpp
    DEPS interi::ecs interi::testing
    LABELS stress)

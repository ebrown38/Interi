# SPDX-License-Identifier: MIT
# Jolt adapter for the job system, kept out of interi::jobs so only physics code links Jolt.
set(_tests ${PROJECT_SOURCE_DIR}/tests/jobs_jolt)

interi_add_module(jobs_jolt
    SOURCES
        src/JoltJobSystem.cpp
    DEPS interi::jobs Jolt::Jolt
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/JoltTestEnvironment.cpp
        ${_tests}/JoltBuildConfigTests.cpp
        ${_tests}/JoltJobSystemTests.cpp)

# Full physics simulations compared across job systems and thread counts (seconds each): label `stress`.
interi_add_test_executable(interi_jobs_jolt_stress_tests
    SOURCES
        ${_tests}/JoltTestEnvironment.cpp
        ${_tests}/stress/JoltDeterminismTests.cpp
    DEPS interi::jobs_jolt interi::testing
    LABELS stress)

# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/jobs)

interi_add_module(jobs
    SOURCES
        src/HeaderCheck.cpp
    DEPS interi::core
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/WorkStealingDequeTests.cpp)

# Long-running and randomized concurrency tests: label `stress`.
interi_add_test_executable(interi_jobs_stress_tests
    SOURCES
        ${_tests}/stress/WorkStealingDequeStressTests.cpp
    DEPS interi::jobs interi::testing
    LABELS stress)

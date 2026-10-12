# SPDX-License-Identifier: MIT
configure_file(src/Version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp @ONLY)

set(_tests ${PROJECT_SOURCE_DIR}/tests/core)

interi_add_module(core
    SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp
        src/Assert.cpp
        src/Log.cpp
    DEPS Threads::Threads
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/VersionTests.cpp
        ${_tests}/AssertTests.cpp
        ${_tests}/ResultTests.cpp
        ${_tests}/LoggerTests.cpp)

target_compile_definitions(interi_core_tests PRIVATE
    INTERI_TEST_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    INTERI_TEST_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    INTERI_TEST_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Long-running and randomized concurrency tests: label `stress`, run nightly (and under TSan).
interi_add_test_executable(interi_core_stress_tests
    SOURCES
        ${_tests}/stress/LoggerStressTests.cpp
    DEPS interi::core interi::testing
    LABELS stress)

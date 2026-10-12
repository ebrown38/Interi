# SPDX-License-Identifier: MIT
configure_file(src/Version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp @ONLY)

interi_add_module(core
    SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp
    TEST_SOURCES
        ${PROJECT_SOURCE_DIR}/tests/core/VersionTests.cpp)

target_compile_definitions(interi_core_tests PRIVATE
    INTERI_TEST_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    INTERI_TEST_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    INTERI_TEST_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH})

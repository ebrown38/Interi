# SPDX-License-Identifier: MIT
# Cross-cutting tests that don't belong to a single module (module unit tests are declared by
# interi_add_module next to each module).

interi_add_test_executable(interi_smoke_tests SOURCES smoke/SmokeTests.cpp LABELS unit)

# --- TDD "pending" mechanism -------------------------------------------------------------
interi_add_test_executable(interi_pending_sample_tests SOURCES meta/PendingSampleTests.cpp LABELS pending)

add_test(NAME Ci_PendingLabelFilter_ExcludesSample
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND} -DBUILD_DIR=${PROJECT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/meta/CheckPendingFilter.cmake)
set_tests_properties(Ci_PendingLabelFilter_ExcludesSample PROPERTIES LABELS unit)

# --- Build-system rules -----------------------------------------------------------------
add_test(NAME InteriModule_MissingTestSources_FailsConfigure
    COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/module_without_tests
            -B ${CMAKE_CURRENT_BINARY_DIR}/module_without_tests
            -DINTERI_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake)
set_tests_properties(InteriModule_MissingTestSources_FailsConfigure PROPERTIES
    LABELS unit
    PASS_REGULAR_EXPRESSION "TEST_SOURCES is required")

# --- The committed starter sample, played headless ----------------------------------------
add_subdirectory(samples)

# --- Engine package: install, then build gameplay modules out of tree ----------------------
add_subdirectory(package)

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

# --- Parser fuzzing over the starter sample's documents (INTERI_FUZZ_ITERATIONS scales it for the nightly run) ----------
add_subdirectory(fuzz)

# --- Platform audit (25.2) ---------------------------------------------------------------------------------------------
add_test(NAME InteriExternalPlatform_Hook_AddsTheFolder
    COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/external_platform_hook
            -B ${CMAKE_CURRENT_BINARY_DIR}/external_platform_hook
            -DINTERI_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake
            -DINTERI_EXTERNAL_PLATFORM_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/external_platform_hook/backend)
set_tests_properties(InteriExternalPlatform_Hook_AddsTheFolder PROPERTIES
    LABELS unit
    PASS_REGULAR_EXPRESSION "hook-ok")
add_test(NAME InteriExternalPlatform_MissingFolder_FailsConfigure
    COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/external_platform_hook
            -B ${CMAKE_CURRENT_BINARY_DIR}/external_platform_hook_missing
            -DINTERI_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake
            -DINTERI_EXTERNAL_PLATFORM_DIR=${CMAKE_CURRENT_BINARY_DIR}/does_not_exist)
set_tests_properties(InteriExternalPlatform_MissingFolder_FailsConfigure PROPERTIES
    LABELS unit
    PASS_REGULAR_EXPRESSION "is[^a-z]+not[^a-z]+a[^a-z]+directory") # CMake wraps long messages

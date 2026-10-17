# SPDX-License-Identifier: MIT
# `coverage` target (INTERI_COVERAGE=ON): merges the .profraw files written by `ctest` and produces
# a text summary plus an HTML report in <build>/coverage/. Run `ctest` first, then build `coverage`.

if(NOT INTERI_COVERAGE)
    return()
endif()

find_program(LLVM_PROFDATA_EXE NAMES llvm-profdata REQUIRED)
find_program(LLVM_COV_EXE NAMES llvm-cov REQUIRED)

set(INTERI_PROFILE_DIR "${PROJECT_BINARY_DIR}/profiles")
set(INTERI_COVERAGE_DIR "${PROJECT_BINARY_DIR}/coverage")

get_property(_test_targets GLOBAL PROPERTY INTERI_TEST_EXECUTABLES)
set(_objects "")
get_property(_shared_libraries GLOBAL PROPERTY INTERI_SHARED_LIBRARIES) # their code isn't in the executables
foreach(_target IN LISTS _test_targets _shared_libraries)
    list(APPEND _objects "-object=$<TARGET_FILE:${_target}>")
endforeach()

# Only Interi sources count; third-party, generated and test code are excluded.
set(_ignore "-ignore-filename-regex=(vcpkg_installed|/build/|/tests/|/external/)")

add_custom_target(coverage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${INTERI_COVERAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -D PROFDATA=${LLVM_PROFDATA_EXE} -D PROFILE_DIR=${INTERI_PROFILE_DIR}
            -D OUTPUT=${INTERI_COVERAGE_DIR}/interi.profdata -P ${CMAKE_CURRENT_LIST_DIR}/MergeProfiles.cmake
    COMMAND ${LLVM_COV_EXE} report -instr-profile=${INTERI_COVERAGE_DIR}/interi.profdata ${_objects} ${_ignore}
    COMMAND ${LLVM_COV_EXE} show -format=html -output-dir=${INTERI_COVERAGE_DIR}/html
            -instr-profile=${INTERI_COVERAGE_DIR}/interi.profdata ${_objects} ${_ignore}
    COMMAND_EXPAND_LISTS
    COMMENT "Generating llvm-cov coverage report in ${INTERI_COVERAGE_DIR}"
    VERBATIM)

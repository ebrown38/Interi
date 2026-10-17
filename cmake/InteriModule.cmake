# SPDX-License-Identifier: MIT
include(GenerateExportHeader)
#
# interi_add_module(<name>
#     SOURCES      <files...>        # library sources (may be empty for header-only modules)
#     TEST_SOURCES <files...>        # REQUIRED: GoogleTest sources for interi_<name>_tests
#     DEPS         <targets...>      # public link dependencies
#     PRIVATE_DEPS <targets...>      # private link dependencies
#     TEST_DEPS    <targets...>      # extra link dependencies for the test executable
#     TEST_LABELS  <labels...>       # CTest labels for the tests (default: unit)
#     SHARED)                        # build as a shared library when INTERI_BUILD_SHARED is ON
#
# Creates library `interi_<name>` (alias `interi::<name>`) with include root `<module dir>/include`,
# and test executable `interi_<name>_tests` registered with CTest. Every module must ship tests:
# configuring a module without TEST_SOURCES is a hard error (TDD rule from the engine plan).
#
# SHARED libraries are the engine that the host and every loaded module share (one copy of each global). On Windows
# every function is exported (WINDOWS_EXPORT_ALL_SYMBOLS); data must not be exported (keep globals in .cpp files
# behind functions). `<interi/<name>/Export.h>` defines INTERI_<NAME>_API for the rare symbol that needs it.

function(interi_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "SHARED" "" "SOURCES;TEST_SOURCES;DEPS;PRIVATE_DEPS;TEST_DEPS;TEST_LABELS")

    if(NOT ARG_TEST_SOURCES)
        message(FATAL_ERROR "interi_add_module(${name}): TEST_SOURCES is required; every module must have unit tests")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "interi_add_module(${name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(target interi_${name})
    if(ARG_SOURCES)
        if(ARG_SHARED AND INTERI_BUILD_SHARED)
            add_library(${target} SHARED ${ARG_SOURCES})
            set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            set_property(GLOBAL APPEND PROPERTY INTERI_SHARED_LIBRARIES ${target})
        else()
            add_library(${target} STATIC ${ARG_SOURCES})
        endif()
        string(TOUPPER "INTERI_${name}" _base)
        set(_export_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
        generate_export_header(${target} BASE_NAME ${_base}
            EXPORT_FILE_NAME "${_export_dir}/interi/${name}/Export.h")
        if(NOT (ARG_SHARED AND INTERI_BUILD_SHARED))
            target_compile_definitions(${target} PUBLIC ${_base}_STATIC_DEFINE)
        endif()
        target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${_export_dir}")
        target_link_libraries(${target} PUBLIC ${ARG_DEPS} PRIVATE ${ARG_PRIVATE_DEPS} interi::warnings)
        if(INTERI_CLANG_TIDY)
            set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${INTERI_CLANG_TIDY_EXE}")
        endif()
    else()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(${target} INTERFACE ${ARG_DEPS})
    endif()
    add_library(interi::${name} ALIAS ${target})

    if(NOT ARG_TEST_LABELS)
        set(ARG_TEST_LABELS unit)
    endif()
    interi_add_test_executable(${target}_tests
        SOURCES ${ARG_TEST_SOURCES}
        DEPS ${target} ${ARG_TEST_DEPS}
        LABELS ${ARG_TEST_LABELS})
endfunction()

# interi_add_test_executable(<target> SOURCES <files...> DEPS <targets...> LABELS <labels...>)
# Builds a GoogleTest executable linked with gtest_main/gmock and discovers its tests into CTest.
function(interi_add_test_executable target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "SHARED" "" "SOURCES;DEPS;LABELS")
    include(GoogleTest)

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_DEPS} GTest::gtest_main GTest::gmock interi::warnings)
    # Discovery runs the executable (--gtest_list_tests). The 5 s default is too tight for profile builds, where
    # Tracy calibrates its timer at process start, on slow shared CI runners.
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "${ARG_LABELS}")
    set_property(GLOBAL APPEND PROPERTY INTERI_TEST_EXECUTABLES ${target})
endfunction()

# interi_add_benchmark(<target> SOURCES <files...> DEPS <targets...>)
# Builds a Google Benchmark executable (with benchmark_main). It is built in every preset so all compilers check
# it, and registered as a CTest `benchmark`-labelled smoke test that runs each benchmark for one iteration
# (--benchmark_dry_run). Measure with the release preset: build/linux-clang-release/benchmarks/<target>.
function(interi_add_benchmark target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "SHARED" "" "SOURCES;DEPS")
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_DEPS} benchmark::benchmark benchmark::benchmark_main
                                            interi::warnings)
    add_test(NAME ${target}_DryRun COMMAND ${target} --benchmark_dry_run)
    set_tests_properties(${target}_DryRun PROPERTIES LABELS benchmark)
endfunction()

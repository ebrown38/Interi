# SPDX-License-Identifier: MIT
#
# interi_add_module(<name>
#     SOURCES      <files...>        # library sources (may be empty for header-only modules)
#     TEST_SOURCES <files...>        # REQUIRED: GoogleTest sources for interi_<name>_tests
#     DEPS         <targets...>      # public link dependencies
#     PRIVATE_DEPS <targets...>      # private link dependencies
#     TEST_DEPS    <targets...>      # extra link dependencies for the test executable
#     TEST_LABELS  <labels...>)      # CTest labels for the tests (default: unit)
#
# Creates library `interi_<name>` (alias `interi::<name>`) with include root `<module dir>/include`,
# and test executable `interi_<name>_tests` registered with CTest. Every module must ship tests:
# configuring a module without TEST_SOURCES is a hard error (TDD rule from the engine plan).

function(interi_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;TEST_SOURCES;DEPS;PRIVATE_DEPS;TEST_DEPS;TEST_LABELS")

    if(NOT ARG_TEST_SOURCES)
        message(FATAL_ERROR "interi_add_module(${name}): TEST_SOURCES is required; every module must have unit tests")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "interi_add_module(${name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    set(target interi_${name})
    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
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
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;DEPS;LABELS")
    include(GoogleTest)

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_DEPS} GTest::gtest_main GTest::gmock interi::warnings)
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${ARG_LABELS}")
    set_property(GLOBAL APPEND PROPERTY INTERI_TEST_EXECUTABLES ${target})
endfunction()

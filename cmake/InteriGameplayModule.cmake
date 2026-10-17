# SPDX-License-Identifier: MIT
# Helpers for building gameplay modules against the engine; installed with the engine package (find_package(Interi)).
#
# interi_add_gameplay_module(<target> SOURCES <files...>)
#   A Module, Extension or Plugin library: a MODULE library linked against the shared engine, named exactly <target>
#   (no "lib" prefix) so the module.json "library" field is the target name.
#
# interi_add_module_load_test(<target> [MODULE_DIR <dir>])
#   A CTest test that loads MODULE_DIR's module.json (default: the current source directory) and <target>'s library
#   through the engine's module host (interi_module_check).

function(interi_add_gameplay_module target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "interi_add_gameplay_module(${target}): SOURCES is required")
    endif()
    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE interi::hotreload)
    set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()

function(interi_add_module_load_test target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "MODULE_DIR" "")
    if(NOT ARG_MODULE_DIR)
        set(ARG_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_test(NAME ${target}_Loads
        COMMAND interi::module_check --search $<TARGET_FILE_DIR:${target}> ${ARG_MODULE_DIR})
endfunction()

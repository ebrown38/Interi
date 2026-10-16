# SPDX-License-Identifier: MIT
# Hot-reloadable gameplay modules: the module ABI, the host that loads, migrates and reloads them, a file watcher.
set(_tests ${PROJECT_SOURCE_DIR}/tests/hotreload)

interi_add_module(hotreload
    SOURCES
        src/FileWatcher.cpp
        src/HeaderCheck.cpp
        src/ModuleHost.cpp
    DEPS interi::ecs interi::platform
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/FileWatcherTests.cpp
        ${_tests}/ModuleHostTests.cpp)

# A module statically links its own copy of these libraries into a shared library.
set_target_properties(interi_core interi_jobs interi_ecs interi_platform interi_hotreload
    PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Test modules built from one source (see TestModule.cpp for the variants); their paths are baked into the tests.
foreach(variant 1 2 3 4 5 6)
    set(_module interi_test_module_v${variant})
    add_library(${_module} MODULE ${_tests}/modules/TestModule.cpp)
    target_link_libraries(${_module} PRIVATE interi::hotreload interi::warnings)
    target_compile_definitions(${_module} PRIVATE INTERI_TEST_MODULE_VARIANT=${variant})
    add_dependencies(interi_hotreload_tests ${_module})
    target_compile_definitions(interi_hotreload_tests PRIVATE
        INTERI_TEST_MODULE_V${variant}="$<TARGET_FILE:${_module}>")
endforeach()

# SPDX-License-Identifier: MIT
# Gameplay modules: the module ABI, the host that loads projects of modules, migrates and reloads them, a file watcher.
set(_tests ${PROJECT_SOURCE_DIR}/tests/hotreload)

# Modules link the shared engine; a static engine can't share its globals, so the module tests need it shared.
set(_module_tests "")
if(INTERI_BUILD_SHARED)
    set(_module_tests ${_tests}/ModuleHostTests.cpp ${_tests}/ModuleProjectTests.cpp ${_tests}/PhysicsEngineHostTests.cpp)
endif()

interi_add_module(hotreload SHARED
    SOURCES
        src/FileWatcher.cpp
        src/HeaderCheck.cpp
        src/ModuleHost.cpp
    DEPS interi::chain interi::runtime interi::values interi::ecs interi::platform interi::sdk
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/FileWatcherTests.cpp
        ${_module_tests})

if(NOT INTERI_BUILD_SHARED)
    # The sandbox still builds its module (it fails to load with Unsupported), so a static engine must be PIC.
    set_target_properties(interi_core interi_jobs interi_ecs interi_platform interi_sdk interi_values interi_chain
        interi_hotreload
        PROPERTIES POSITION_INDEPENDENT_CODE ON)
    return()
endif()

# Test modules built from one source (see TestModule.cpp for the variants); their paths are baked into the tests.
foreach(variant 1 2 3 4 5 6 7)
    set(_module interi_test_module_v${variant})
    add_library(${_module} MODULE ${_tests}/modules/TestModule.cpp)
    target_link_libraries(${_module} PRIVATE interi::hotreload interi::warnings)
    target_compile_definitions(${_module} PRIVATE INTERI_TEST_MODULE_VARIANT=${variant})
    add_dependencies(interi_hotreload_tests ${_module})
    target_compile_definitions(interi_hotreload_tests PRIVATE
        INTERI_TEST_MODULE_V${variant}="$<TARGET_FILE:${_module}>")
endforeach()

# The libraries of the shared SDK test project (tests/sdk/modules/*/module.json name them), in one directory.
set(_sdk_library_dir ${CMAKE_CURRENT_BINARY_DIR}/sdk_test_modules)
set(_sdk_variant 1)
foreach(name ext a b plugin)
    set(_module interi_sdk_test_${name})
    add_library(${_module} MODULE ${_tests}/modules/SdkTestModule.cpp)
    target_link_libraries(${_module} PRIVATE interi::hotreload interi::warnings)
    target_compile_definitions(${_module} PRIVATE INTERI_SDK_TEST_MODULE=${_sdk_variant})
    set_target_properties(${_module} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${_sdk_library_dir})
    add_dependencies(interi_hotreload_tests ${_module})
    math(EXPR _sdk_variant "${_sdk_variant} + 1")
endforeach()
target_compile_definitions(interi_hotreload_tests PRIVATE
    INTERI_SDK_TEST_LIBRARY_DIR="${_sdk_library_dir}"
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")

# The Physics Engine selection fixtures (tests/hotreload/physics/*/module.json name them), in one directory.
set(_physics_library_dir ${CMAKE_CURRENT_BINARY_DIR}/physics_test_modules)
set(_physics_variant 1)
foreach(name alpha beta alpha_plus game game_alpha game_beta thief)
    set(_module interi_physics_${name})
    add_library(${_module} MODULE ${_tests}/modules/PhysicsTestModule.cpp)
    target_link_libraries(${_module} PRIVATE interi::hotreload interi::warnings)
    target_compile_definitions(${_module} PRIVATE INTERI_PHYSICS_TEST_MODULE=${_physics_variant})
    set_target_properties(${_module} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${_physics_library_dir})
    add_dependencies(interi_hotreload_tests ${_module})
    math(EXPR _physics_variant "${_physics_variant} + 1")
endforeach()
target_compile_definitions(interi_hotreload_tests PRIVATE
    INTERI_PHYSICS_TEST_LIBRARY_DIR="${_physics_library_dir}"
    INTERI_PHYSICS_TEST_MODULES_DIR="${_tests}/physics")

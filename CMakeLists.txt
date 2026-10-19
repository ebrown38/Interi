# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.29)

# vcpkg is vendored as a submodule so every machine and CI runner uses the same pinned ports.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain")
endif()

project(Interi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # no C++20 modules; skip per-file dependency scanning

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(INTERI_CLANG_TIDY "Run clang-tidy on engine (non-test) targets" OFF)
set(INTERI_SANITIZER "" CACHE STRING "Sanitizer build: '', 'asan' (ASan+UBSan), 'tsan'")
option(INTERI_COVERAGE "Instrument for llvm-cov source coverage (Clang only)" OFF)
option(INTERI_PROFILING "Link Tracy and enable the INTERI_ZONE profiling macros" OFF)
set(INTERI_EXTERNAL_PLATFORM_DIR "" CACHE PATH "A platform backend project outside this repository (see cmake/InteriExternalPlatform.cmake)")
option(INTERI_BUILD_SHARED "Build the engine core libraries as shared libraries (required by gameplay modules)" ON)

# Windows finds DLLs next to the executable, so every executable and engine DLL goes to one directory (test discovery
# runs the test executables at build time).
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

if(INTERI_CLANG_TIDY)
    find_program(INTERI_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

include(CTest)
include(InteriArch)
include(InteriWarnings)
include(InteriSanitizers)
include(InteriBuildSettings)
include(InteriModule)
include(InteriShaders)
include(InteriExternalPlatform)

find_package(GTest CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_subdirectory(testing)
add_subdirectory(engine)
add_subdirectory(tests)
add_subdirectory(benchmarks)
add_subdirectory(tools)
if(INTERI_EXTERNAL_PLATFORM_DIR)
    interi_add_external_platform("${INTERI_EXTERNAL_PLATFORM_DIR}")
endif()
if(INTERI_BUILD_SHARED)
    add_subdirectory(samples/modules/starter) # a sample Module, built like any out-of-tree one
endif()

# Optional module projects kept next to the engine (gitignored /modules/<name>/, e.g. the Jolt Physics Engine): each
# folder with a CMakeLists.txt is built in tree. Absent folders change nothing. -DINTERI_MODULE_DIRS= skips them all.
if(NOT DEFINED INTERI_MODULE_DIRS)
    file(GLOB INTERI_MODULE_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/modules/*")
endif()
foreach(_module_dir IN LISTS INTERI_MODULE_DIRS)
    if(EXISTS "${_module_dir}/CMakeLists.txt")
        message(STATUS "Module project: ${_module_dir}")
        cmake_path(GET _module_dir FILENAME _module_name)
        add_subdirectory("${_module_dir}" "modules/${_module_name}")
    endif()
endforeach()

include(InteriPackage) # after the libraries and tools/module_check
include(InteriCoverage) # after all test executables are declared

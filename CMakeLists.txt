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

if(INTERI_CLANG_TIDY)
    find_program(INTERI_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

include(CTest)
include(InteriArch)
include(InteriWarnings)
include(InteriSanitizers)
include(InteriModule)

find_package(GTest CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_subdirectory(testing)
add_subdirectory(engine)
add_subdirectory(tests)
add_subdirectory(benchmarks)
add_subdirectory(tools)

include(InteriCoverage) # after all test executables are declared

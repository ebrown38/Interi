# SPDX-License-Identifier: MIT
# Starter (com.interi.starter): an Interi Module. Built out of tree against an installed engine (find_package(Interi)),
# or in tree as part of the engine's build (the sample tests run its project headless).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.29)
    project(com_interi_starter LANGUAGES CXX)
    find_package(Interi REQUIRED)
else()
    include(${PROJECT_SOURCE_DIR}/cmake/InteriGameplayModule.cmake)
endif()

interi_add_gameplay_module(com_interi_starter SOURCES src/StarterModule.cpp)

# ctest: loads this folder's module.json and the library through the engine's module host.
enable_testing()
interi_add_module_load_test(com_interi_starter)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set_tests_properties(com_interi_starter_Loads PROPERTIES LABELS integration) # in tree: CI runs it
endif()

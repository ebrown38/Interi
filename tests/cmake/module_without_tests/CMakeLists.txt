# SPDX-License-Identifier: MIT
# Fixture for InteriModule_MissingTestSources_FailsConfigure: configuring this project must fail.
cmake_minimum_required(VERSION 3.29)
project(ModuleWithoutTests LANGUAGES NONE)
include("${INTERI_CMAKE_DIR}/InteriModule.cmake")
interi_add_module(no_tests SOURCES dummy.cpp)

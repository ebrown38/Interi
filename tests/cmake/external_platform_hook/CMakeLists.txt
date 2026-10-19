# SPDX-License-Identifier: MIT
# Fixture for the InteriExternalPlatform_* tests: adds the folder INTERI_EXTERNAL_PLATFORM_DIR names, like the root
# CMakeLists.txt does.
cmake_minimum_required(VERSION 3.29)
project(ExternalPlatformHook LANGUAGES NONE)
include("${INTERI_CMAKE_DIR}/InteriExternalPlatform.cmake")
interi_add_external_platform("${INTERI_EXTERNAL_PLATFORM_DIR}")
if(NOT TARGET fake_backend)
    message(FATAL_ERROR "the external platform's targets are missing")
endif()
message(STATUS "hook-ok")

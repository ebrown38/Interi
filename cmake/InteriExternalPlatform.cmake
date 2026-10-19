# SPDX-License-Identifier: MIT
#
# interi_add_external_platform(<dir>)
#
# The hook for platform backends that live outside this repository (the private PS5/Xbox repos, or any other port):
# `-DINTERI_EXTERNAL_PLATFORM_DIR=<path>` adds that folder's CMakeLists.txt after the engine's own libraries, so it can
# link `interi::rhi`, `interi::platform` and the rest and add its own RHI device, window/event source and file system.
# Nothing in this repository refers to a backend by name: the folder defines whatever targets it needs, and the player
# (or editor) of that platform links them. Backend headers (console SDKs, D3D12) never enter this tree: the platform
# audit (tools/platform_audit) only allows them inside such a folder.
function(interi_add_external_platform dir)
    if(NOT IS_DIRECTORY "${dir}")
        message(FATAL_ERROR "INTERI_EXTERNAL_PLATFORM_DIR: '${dir}' is not a directory")
    endif()
    if(NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR "INTERI_EXTERNAL_PLATFORM_DIR: '${dir}' has no CMakeLists.txt")
    endif()
    message(STATUS "External platform: ${dir}")
    add_subdirectory("${dir}" external_platform)
endfunction()
